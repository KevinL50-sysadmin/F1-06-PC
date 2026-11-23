"""Keep #known-issues and #announcements in step with each release.

Dry run by default: prints the message it would send. --publicar sends it through a webhook,
whose URL comes from an environment variable (a GitHub secret in Actions):

  known-issues  DISCORD_WEBHOOK_KNOWN_ISSUES   edits the game's message in #known-issues
  anuncio       DISCORD_WEBHOOK_ANNOUNCEMENTS  posts the release announcement

Examples:
  python sync.py known-issues --juego f1-06 --version 1.2.0 --fuente discord/known-issues.md
  python sync.py anuncio --juego f1-06 --version v1.2.0 --changelog CHANGELOG.md --publicar
  python sync.py anuncio ... --texto discord/anuncios/1.2.0.md   (hand-written text, if the file exists)

Exit codes: 0 done, 1 invalid input or Discord error, 2 missing webhook variable.
"""
import argparse
import json
import os
import pathlib
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from discord_api import DiscordError, Webhook  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
# juegos.json next to the script (copy in a game repo) or at the root of the tools repo
HERE = pathlib.Path(__file__).resolve().parent
DEFAULT_CONFIG = HERE / "juegos.json" if (HERE / "juegos.json").exists() else ROOT / "juegos.json"
LIMIT = 2000
MORE = "- and more, see the full changelog."
FIXED_RE = re.compile(r"\s*\(fixed-in:\s*([^)]+)\)\s*$")
CHANNEL_RE = re.compile(r"(?<![\w&<])#([a-z][a-z0-9-]*)")
ENV = {"known-issues": "DISCORD_WEBHOOK_KNOWN_ISSUES", "anuncio": "DISCORD_WEBHOOK_ANNOUNCEMENTS"}


class SyncError(ValueError):
    pass


def parse_version(text):
    m = re.fullmatch(r"v?(\d+)\.(\d+)\.(\d+)", text.strip())
    if not m:
        raise SyncError(f"invalid version {text}")
    return tuple(int(x) for x in m.groups())


def show_version(text):
    return ".".join(map(str, parse_version(text)))


def game(config, key):
    try:
        return config["juegos"][key]
    except KeyError:
        raise SyncError(f"unknown game {key}") from None


def fix_style(text):
    """The server never uses em or en dashes (owner's rule)."""
    return text.replace("—", "-").replace("–", "-")


def check_length(text):
    if len(text) > LIMIT:
        raise SyncError(f"message has {len(text)} characters, Discord allows {LIMIT}")


def mentions(text, config):
    def repl(m):
        name = m.group(1)
        if name not in config["canales"]:
            raise SyncError(f"unknown channel #{name}")
        return f"<#{config['canales'][name]}>"
    return CHANNEL_RE.sub(repl, text)


def bullets(text):
    """Top-level '- ' items; indented lines continue the previous item; anything else is ignored."""
    items, current = [], None
    for line in text.splitlines():
        if line.startswith("- "):
            current = [line[2:].strip()]
            items.append(current)
        elif current is not None and line.startswith("  ") and line.strip():
            current.append(line.strip())
        else:
            current = None
    return [" ".join(parts) for parts in items]


def render_known_issues(source, config, key, version):
    g = game(config, key)
    released = parse_version(version)
    lines = [f"**{g['nombre']} v{show_version(version)}** (current release)"]
    for item in bullets(source):
        m = FIXED_RE.search(item)
        if m:
            fixed = parse_version(m.group(1))
            if fixed <= released:
                continue
            item = item[:m.start()] + f" **Fixed in {show_version(m.group(1))}**."
        lines.append("- " + item)
    if len(lines) == 1:
        lines.append("- No known issues right now.")
    text = "\n".join(lines)
    if g.get("pie"):
        text += "\n\n" + g["pie"]
    text = fix_style(mentions(text, config))
    check_length(text)
    return text


def changelog_section(changelog, version):
    v = show_version(version)
    m = re.search(rf"^## \[{re.escape(v)}\][^\n]*\n(.*?)(?=^## |\Z)", changelog, re.M | re.S)
    if not m:
        raise SyncError(f"no section [{v}] in the changelog")
    return m.group(1).strip()


def summary(item):
    item = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", item)
    item = re.sub(r"\*\*([^*]+)\*\*", r"\1", item)
    m = re.search(r"\.(\s|$)", item)
    return item[:m.start() + 1] if m else item


def render_announcement(changelog, config, key, version, written=None):
    """Summary of the changelog section, or the hand-written text when there is one."""
    g = game(config, key)
    v = show_version(version)
    head = f"\U0001F3C1 **{g['nombre']} v{v} is out!**"
    foot = (f"Full changelog and download: <{g['repo'].rstrip('/')}/releases/tag/v{v}>\n"
            f"Talk about it in <#{config['canales'][g['canal_juego']]}>, "
            f"report problems in <#{config['canales']['problems-and-errors']}>.")
    section = changelog_section(changelog, version)
    if written is not None:
        own = "\n".join(l for l in written.strip().splitlines() if not l.startswith("# ")).strip()
        text = fix_style(f"{head}\n\n{mentions(own, config)}\n\n{foot}")
        check_length(text)
        return text
    body = []
    for block in re.split(r"^### ", section, flags=re.M):
        if not block.strip():
            continue
        title, _, rest = block.partition("\n")
        items = bullets(rest)
        if items:
            body.append(("", f"**{title.strip()}**"))
            body.extend(("item", "- " + fix_style(summary(i))) for i in items)
    out = [head, ""]
    budget = LIMIT - len(head) - len(foot) - len(MORE) - 8
    used, cut = 0, False
    for kind, line in body:
        if used + len(line) + 2 > budget:
            cut = True
            break
        if kind == "" and len(out) > 2:
            out.append("")
            used += 1
        out.append(line)
        used += len(line) + 1
    if cut:
        out.append(MORE)
    text = fix_style("\n".join(out) + "\n\n" + foot)
    check_length(text)
    return text


def main(argv=None):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(description="Update #known-issues and #announcements from the game repo.")
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name, extra in (("known-issues", "--fuente"), ("anuncio", "--changelog")):
        p = sub.add_parser(name)
        p.add_argument("--juego", required=True, help="game key in juegos.json, e.g. f1-06")
        p.add_argument("--version", required=True, help="released version, 1.2.0 or v1.2.0")
        p.add_argument(extra, required=True, dest="fuente", help="markdown file in the game repo")
        if name == "anuncio":
            p.add_argument("--texto", help="hand-written announcement (used only if the file exists; lines starting"
                                            " with '# ' are notes)")
        p.add_argument("--config", default=str(DEFAULT_CONFIG))
        p.add_argument("--publicar", action="store_true", help="send it (default: dry run)")
    args = ap.parse_args(argv)

    url = os.environ.get(ENV[args.cmd])
    if args.publicar and not url:
        print(f"error: {ENV[args.cmd]} is not set", file=sys.stderr)
        return 2
    try:
        config = json.loads(pathlib.Path(args.config).read_text(encoding="utf-8"))
        source = pathlib.Path(args.fuente).read_text(encoding="utf-8")
        g = game(config, args.juego)
        if args.cmd == "known-issues":
            text = render_known_issues(source, config, args.juego, args.version)
            mid = g.get("mensaje_known_issues")
            action = f"would edit message {mid}" if mid else "would post a new message"
        else:
            texto = pathlib.Path(args.texto) if args.texto else None
            written = texto.read_text(encoding="utf-8") if texto and texto.is_file() else None
            text = render_announcement(source, config, args.juego, args.version, written)
            mid, action = None, "would post a new message"
        if not args.publicar:
            print(f"--- {args.cmd} {args.juego} ({action}) ---\n{text}\n---")
            print("Dry run: nothing was sent. Add --publicar to send.")
            return 0
        hook = Webhook(url)
        if mid:
            hook.edit(mid, text)
            print(f"Edited message {mid}.")
        else:
            msg = hook.post(text)
            print(f"Posted message {msg['id']}.")
            if args.cmd == "known-issues":
                print(f"Save it as mensaje_known_issues of {args.juego} in juegos.json.")
        return 0
    except (SyncError, DiscordError, OSError) as err:
        print(f"error: {err}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
