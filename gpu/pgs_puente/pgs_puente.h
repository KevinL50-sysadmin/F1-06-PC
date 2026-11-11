/*
 * pgs_puente: a small C API over paraLLEl-GS (https://github.com/Arntzen-Software/parallel-gs, LGPL-3.0+).
 *
 * The DLL statically links paraLLEl-GS and Granite; the game runtime only uses the functions below, loaded with
 * LoadLibrary/GetProcAddress, so the DLL can be rebuilt or replaced on its own (see LICENSE.txt).
 *
 * Threading: a context and its Vulkan device are not thread safe. Create, use and destroy each context on ONE thread;
 * only pgs_get_pipeline_cache_size and pgs_get_pipeline_cache may run on another thread meanwhile (one at a time, and
 * not during pgs_destroy). Pointers returned by the map functions and pgs_get_frame are valid until the next call on
 * that context.
 */
#ifndef PGS_PUENTE_H
#define PGS_PUENTE_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(PGS_PUENTE_BUILD)
#define PGS_API __declspec(dllexport)
#else
#define PGS_API __declspec(dllimport)
#endif
#else
#define PGS_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 2: PGS_SCANOUT_SPLIT_CIRCUITS and pgs_frame.circuits/scanout. 3: pgs_vsync_composed and pgs_frame.composed.
   4: pgs_create_info.flags (PGS_CREATE_*), pgs_set_quality, pgs_get_super_sampling and pgs_frame.scale/super_sampling.
   5: the pipeline cache (pgs_create_info.pipeline_cache*, pgs_get_pipeline_cache[_size]) and
   pgs_get_device_identity. */
#define PGS_PUENTE_API_VERSION 5u
#define PGS_VRAM_SIZE (4u * 1024u * 1024u)

typedef struct pgs_context pgs_context;

enum
{
    /* Room for the highest rate the GPU supports (~144 MB of local memory copies at 16x), so that pgs_set_quality can
       raise the rate later; without it the rate can only go down to 1x and back. */
    PGS_CREATE_DYNAMIC_SUPER_SAMPLING = 1u << 0,
    /* Super-sampled textures (paraLLEl-GS's super_sampled_textures): sprites that read a super-sampled frame keep its
       samples (blits, post-processing passes). Off at 1x. Also a pgs_set_quality flag. */
    PGS_CREATE_SUPER_SAMPLED_TEXTURES = 1u << 1,
};

/* What a pipeline cache belongs to: VkPhysicalDeviceProperties' vendorID, deviceID, driverVersion and
   pipelineCacheUUID. Another GPU or another driver cannot use it. */
typedef struct pgs_device_identity
{
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t driver_version;
    uint8_t pipeline_cache_uuid[16];
} pgs_device_identity;

typedef struct pgs_create_info
{
    uint32_t struct_size;    /* sizeof(pgs_create_info) */
    uint32_t super_sampling; /* 1, 2, 4, 8 or 16 (others: 1); limited to what the GPU supports */
    uint32_t flags;          /* PGS_CREATE_* */
    /* A pipeline cache saved before with pgs_get_pipeline_cache (NULL: none), and the device it was saved on: it is
       only used when that is exactly this device and driver, otherwise the device starts an empty cache. Either way
       the pipelines compiled from now on go into the device's cache. Only read during pgs_create. */
    pgs_device_identity pipeline_cache_device;
    const void *pipeline_cache;
    size_t pipeline_cache_size;
} pgs_create_info;

/* The privileged GS registers that drive the scanout (CRTC). */
typedef struct pgs_priv_regs
{
    uint64_t pmode;
    uint64_t smode1;
    uint64_t smode2;
    uint64_t dispfb1;
    uint64_t display1;
    uint64_t dispfb2;
    uint64_t display2;
    uint64_t extbuf;
    uint64_t extdata;
    uint64_t extwrite;
    uint64_t bgcolor;
} pgs_priv_regs;

enum
{
    PGS_SCANOUT_FORCE_PROGRESSIVE = 1u << 0,
    PGS_SCANOUT_OVERSCAN = 1u << 1,
    PGS_SCANOUT_ANTI_BLUR = 1u << 2,
    PGS_SCANOUT_CRTC_OFFSETS = 1u << 3,
    PGS_SCANOUT_ADAPT_HORIZONTAL = 1u << 4,
    /* Twice the size on each axis from the super samples (paraLLEl-GS's high_resolution_scanout): only with
       PGS_SCANOUT_FORCE_PROGRESSIVE and 4x or more; otherwise ignored (pgs_frame.scale says which). */
    PGS_SCANOUT_HIGH_RES = 1u << 5,
    /* Each enabled circuit is scanned out alone, unblended and with its alpha (PMODE MMOD 1, ALP 0xFF; SLBG 0), and the
       images are read back one under the other, circuit 1 first: the caller merges them (pgs_frame.circuits). */
    PGS_SCANOUT_SPLIT_CIRCUITS = 1u << 6,
};

typedef struct pgs_frame
{
    const uint8_t *rgba; /* width * height RGBA8 pixels, rows packed */
    uint32_t width;
    uint32_t height;
    uint32_t mode_width;  /* video mode size, for the aspect ratio */
    uint32_t mode_height;
    uint32_t interlaced;
    uint64_t serial; /* 1 for the first frame read back, then one more per frame */
    uint32_t circuits; /* PGS_SCANOUT_SPLIT_CIRCUITS: the PMODE EN bits of the stacked images (1, 2 or 3); else 0 */
    uint32_t composed; /* 1: made by pgs_vsync_composed (the merged frame, alpha 0xFF; circuits = the ones shown) */
    uint64_t scanout; /* which pgs_vsync made it: 1 for the first one that returned 1, then one more per such call */
    uint32_t scale;   /* 2: a high resolution scanout (PGS_SCANOUT_HIGH_RES), twice the size on each axis; else 1 */
    uint32_t super_sampling; /* the rate it was drawn with (1..16) */
} pgs_frame;

typedef struct pgs_stats
{
    uint64_t gif_bytes;      /* GIF payload bytes received */
    uint64_t gif_transfers;  /* pgs_gif_transfer calls */
    uint64_t vsyncs;         /* scanouts requested */
    uint64_t frames;         /* scanouts read back */
    uint64_t fifo_bytes;     /* local -> host FIFO bytes read */
    uint64_t vram_maps;      /* pgs_map_vram_read/write calls */
} pgs_stats;

PGS_API uint32_t pgs_api_version(void);

/* NULL on failure (no Vulkan loader, no device, ...), with a message in error (always NUL-terminated). */
PGS_API pgs_context *pgs_create(const pgs_create_info *info, char *error, size_t error_size);
PGS_API void pgs_destroy(pgs_context *context);

/* The GPU's name, for the log. */
PGS_API const char *pgs_device_name(pgs_context *context);
/* The device a pipeline cache saved now belongs to (pgs_create_info.pipeline_cache_device). */
PGS_API void pgs_get_device_identity(pgs_context *context, pgs_device_identity *identity);
/* The size of the device's pipeline cache as pgs_get_pipeline_cache would copy it now (0: none): to see if it grew
   since it was last saved. */
PGS_API size_t pgs_get_pipeline_cache_size(pgs_context *context);
/* A copy of the device's pipeline cache, as pgs_create_info.pipeline_cache takes it (Granite's blob: the
   pipelineCacheUUID, a hash and the driver's data), in *size bytes; NULL (and 0) on failure. Valid until the next
   pgs_get_pipeline_cache on that context. paraLLEl-GS may still be compiling on its own threads (its precompiled
   shader variants) and the cache grows meanwhile: the copy is retried until it is whole. Takes about as
   long as the driver needs to serialize it (~60 ms for 50 MB). */
PGS_API const void *pgs_get_pipeline_cache(pgs_context *context, size_t *size);

/* GIF data as the GS receives it on PATH1/2/3 (path 0..3): whole 128-bit words, size a multiple of 16. */
PGS_API void pgs_gif_transfer(pgs_context *context, uint32_t path, const void *data, size_t size_bytes);
/* An A+D register write (0x00..0x7F), as GIF A+D would do it. */
PGS_API void pgs_write_register(pgs_context *context, uint32_t address, uint64_t value);
/* Forgets any partially received GIF tag of a path (after a truncated packet). */
PGS_API void pgs_reset_gif_path(pgs_context *context, uint32_t path);
/* Submits pending GPU work. */
PGS_API void pgs_flush(pgs_context *context);

/* Local memory in the GS's own layout (offset/size in bytes), waiting for the GPU work that writes it. */
PGS_API const void *pgs_map_vram_read(pgs_context *context, size_t offset, size_t size);
PGS_API void *pgs_map_vram_write(pgs_context *context, size_t offset, size_t size);
PGS_API void pgs_end_vram_write(pgs_context *context, size_t offset, size_t size);

/* The data of the current local -> host transfer (TRXDIR = 1), in 128-bit words; zeros past its end. */
PGS_API void pgs_read_transfer_fifo(pgs_context *context, void *data, uint32_t num_128b_words);

/* Scans out the display (as at a vertical blank) with these registers and field (0/1) and starts reading the
   image back. Returns 1 if there was an image (one per enabled circuit with PGS_SCANOUT_SPLIT_CIRCUITS), 0 if not
   (display off, unknown video mode). */
PGS_API int pgs_vsync(pgs_context *context, const pgs_priv_regs *regs, uint32_t field, uint32_t scanout_flags);
/* How pgs_vsync_composed merges the circuits: as a CPU presenter that reads the display in pixels (our runtime's
   GSCpuBackend). Out of the circuits in circuits, each scanned out alone (PGS_SCANOUT_SPLIT_CIRCUITS), it makes a
   width x height frame: with both, BGCOLOR, then circuit 2 inside its size unless PMODE SLBG, then circuit 1 inside
   its size blended with ALP (PMODE MMOD 1) or min(255, 2 * its alpha) (MMOD 0) as dst + (src - dst) * factor / 255
   truncated; with one, that circuit. Pixels past a circuit's image are black; 16-bit circuits (ct16) have their
   colours widened as (c << 3) | (c >> 2); alpha is 0xFF. Only that frame is read back. */
typedef struct pgs_compose_info
{
    uint32_t struct_size; /* sizeof(pgs_compose_info) */
    uint32_t circuits;    /* the PMODE EN bits of the circuits shown (1, 2 or 3), each enabled in PMODE */
    uint32_t ct16;        /* bit n: circuit n + 1 is 16-bit (PSMCT16/PSMCT16S) */
    uint32_t width;       /* the frame's size, 1..2048 */
    uint32_t height;
    uint32_t circuit_width[2]; /* each circuit's picture size, in scanout pixels from the top left corner */
    uint32_t circuit_height[2];
} pgs_compose_info;

/* As pgs_vsync with PGS_SCANOUT_SPLIT_CIRCUITS for the circuits in compose->circuits, but the images are merged on
   the GPU (see pgs_compose_info) and only the merged frame is read back (pgs_frame.composed = 1). Returns 1 if every
   circuit had an image, else 0. With a high resolution scanout (PGS_SCANOUT_HIGH_RES, pgs_frame.scale 2) every size of
   compose is doubled: the frame is (2 * width) x (2 * height). */
PGS_API int pgs_vsync_composed(pgs_context *context, const pgs_priv_regs *regs, uint32_t field, uint32_t scanout_flags,
                               const pgs_compose_info *compose);
/* The newest frame read back. wait = 0: only if its readback is finished. Returns 1 with *frame filled, else 0. */
PGS_API int pgs_get_frame(pgs_context *context, int wait, pgs_frame *frame);

PGS_API void pgs_get_stats(pgs_context *context, pgs_stats *stats);

/* Changes the super-sampling rate (1, 2, 4, 8 or 16) and PGS_CREATE_SUPER_SAMPLED_TEXTURES at once, between frames:
   it waits for the GPU and clears the super samples (the next frame is drawn again at the new rate). Above the
   creation rate only with PGS_CREATE_DYNAMIC_SUPER_SAMPLING. Returns the rate in use afterwards. */
PGS_API uint32_t pgs_set_quality(pgs_context *context, uint32_t super_sampling, uint32_t flags);
/* The super-sampling rate in use (1..16). */
PGS_API uint32_t pgs_get_super_sampling(pgs_context *context);

#ifdef __cplusplus
}
#endif

#endif
