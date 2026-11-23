"""Minimal Discord API v10 client (stdlib only).

Two kinds of credentials:
- bot token: local only, read from DISCORD_BOT_TOKEN or ~/.discord-recomps-token
  (lines token=... and servidor=...). Never put it in GitHub.
- webhook URL: the only secret GitHub Actions get. A webhook can post and edit its own messages.
"""
import json
import os
import pathlib
import re
import time
import urllib.error
import urllib.request

API = "https://discord.com/api/v10"
TOKEN_FILE = pathlib.Path.home() / ".discord-recomps-token"
USER_AGENT = "DiscordBot (https://discord.com/developers/docs, 1)"


class DiscordError(RuntimeError):
    pass


def _safe(url):
    """URL fit for logs: no query, webhook token hidden."""
    return re.sub(r"(/webhooks/\d+/)[^/?]+", r"\1***", url.split("?")[0])


def _request(method, url, body=None, headers=None):
    data = None if body is None else json.dumps(body).encode("utf-8")
    hdrs = {"Content-Type": "application/json", "User-Agent": USER_AGENT}
    hdrs.update(headers or {})
    for _ in range(5):
        req = urllib.request.Request(url, data=data, method=method, headers=hdrs)
        try:
            with urllib.request.urlopen(req, timeout=30) as resp:
                raw = resp.read()
                return json.loads(raw) if raw else None
        except urllib.error.HTTPError as err:
            text = err.read().decode("utf-8", "replace")
            if err.code == 429:
                try:
                    wait = float(json.loads(text).get("retry_after", 1))
                except (ValueError, AttributeError):
                    wait = 1.0
                time.sleep(min(wait, 60) + 0.2)
                continue
            raise DiscordError(f"{method} {_safe(url)} -> {err.code}: {text[:300]}") from None
        except urllib.error.URLError as err:
            raise DiscordError(f"{method} {_safe(url)}: {err.reason}") from None
    raise DiscordError(f"{method} {_safe(url)}: rate limited too many times")


def bot_credentials():
    token = os.environ.get("DISCORD_BOT_TOKEN")
    guild = os.environ.get("DISCORD_GUILD_ID")
    if not (token and guild) and TOKEN_FILE.exists():
        pairs = dict(l.split("=", 1) for l in TOKEN_FILE.read_text().split() if "=" in l)
        token = token or pairs.get("token", "").strip()
        guild = guild or pairs.get("servidor", "").strip()
    if not (token and guild):
        raise DiscordError("no bot token: set DISCORD_BOT_TOKEN and DISCORD_GUILD_ID")
    return token, guild


class Bot:
    def __init__(self):
        self.token, self.guild = bot_credentials()

    def call(self, method, path, body=None):
        return _request(method, API + path, body, {"Authorization": "Bot " + self.token})


class Webhook:
    """Webhook from its URL https://discord.com/api/webhooks/<id>/<token>."""

    def __init__(self, url):
        if not url or "/webhooks/" not in url:
            raise DiscordError("invalid webhook URL")
        self.url = url.split("?")[0].rstrip("/")

    def post(self, content, username=None):
        body = {"content": content, "allowed_mentions": {"parse": []}, "flags": 4}
        if username:
            body["username"] = username
        return _request("POST", self.url + "?wait=true", body)

    def edit(self, message_id, content):
        body = {"content": content, "allowed_mentions": {"parse": []}, "flags": 4}
        return _request("PATCH", f"{self.url}/messages/{message_id}", body)

    def get(self, message_id):
        return _request("GET", f"{self.url}/messages/{message_id}")
