// pgs_puente: C API over paraLLEl-GS (see pgs_puente.h). Statically links paraLLEl-GS (LGPL-3.0+) and Granite (MIT).

#define PGS_PUENTE_BUILD 1
#include "pgs_puente.h"

#include "context.hpp"
#include "device.hpp"
#include "gs_interface.hpp"

#include <stdint.h>
#include "compose_circuits.spv.h" // kComposeCircuitsSpirv, built from compose_circuits.comp

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace ParallelGS;

namespace
{
    void copyError(char *error, size_t errorSize, const char *message)
    {
        if (error == nullptr || errorSize == 0u)
            return;
        std::snprintf(error, errorSize, "%s", message);
    }

    // A scanout image copied to a host buffer; finished when its fence signals.
    struct Readback
    {
        Vulkan::BufferHandle buffer;
        Vulkan::BufferHandle images; // pgs_vsync_composed: the circuit images, on the GPU
        Vulkan::Fence fence;
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint32_t modeWidth = 0u;
        uint32_t modeHeight = 0u;
        uint32_t circuits = 0u;
        uint32_t scale = 1u;
        uint32_t superSampling = 1u;
        bool interlaced = false;
        bool composed = false;
        bool pending = false;
    };

    SuperSampling superSamplingOf(uint32_t rate)
    {
        switch (rate)
        {
        case 2u: return SuperSampling::X2;
        case 4u: return SuperSampling::X4;
        case 8u: return SuperSampling::X8;
        case 16u: return SuperSampling::X16;
        default: return SuperSampling::X1;
        }
    }

    // GSRenderer::get_max_supported_super_sampling (not public): 8x and 16x need full subgroups of 8 and 16 lanes.
    uint32_t maxSupportedSuperSampling(Vulkan::Device &device)
    {
        if (device.supports_subgroup_size_log2(true, 4, 6))
            return 16u;
        if (device.supports_subgroup_size_log2(true, 3, 6))
            return 8u;
        return 4u;
    }

    pgs_device_identity identityOf(const VkPhysicalDeviceProperties &properties)
    {
        pgs_device_identity identity = {};
        identity.vendor_id = properties.vendorID;
        identity.device_id = properties.deviceID;
        identity.driver_version = properties.driverVersion;
        static_assert(sizeof(identity.pipeline_cache_uuid) == sizeof(properties.pipelineCacheUUID), "UUID size");
        std::memcpy(identity.pipeline_cache_uuid, properties.pipelineCacheUUID, sizeof(identity.pipeline_cache_uuid));
        return identity;
    }

    bool sameDevice(const pgs_device_identity &a, const pgs_device_identity &b)
    {
        return a.vendor_id == b.vendor_id && a.device_id == b.device_id && a.driver_version == b.driver_version &&
               std::memcmp(a.pipeline_cache_uuid, b.pipeline_cache_uuid, sizeof(a.pipeline_cache_uuid)) == 0;
    }

    // compose_circuits.comp's push constants.
    struct ComposeRegisters
    {
        uint32_t outWidth, outHeight, flags, alp, bgcolor, offset1, offset2;
        uint32_t imageWidth1, imageHeight1, imageWidth2, imageHeight2, width1, height1, width2, height2;
    };
}

struct pgs_context
{
    Vulkan::Context vulkan;
    Vulkan::Device device;
    GSInterface gs;
    std::string deviceName;
    pgs_device_identity identity{};
    bool pipelineCache = false;         // the device has a VkPipelineCache (pgs_get_pipeline_cache)
    std::vector<uint8_t> pipelineCacheCopy; // handed out by pgs_get_pipeline_cache
    Vulkan::Program *compose = nullptr; // compose_circuits.comp, owned by the device
    uint32_t superSampling = 1u;        // the rate in use
    uint32_t maxSuperSampling = 1u;     // the highest one local memory has room for

    static constexpr uint32_t kReadbacks = 3u;
    Readback readbacks[kReadbacks];
    uint32_t nextReadback = 0u;
    uint64_t readbackSerial = 0u; // serial of the newest readback started
    uint64_t readbackSerials[kReadbacks] = {};

    std::vector<uint8_t> frame; // the frame handed out by pgs_get_frame
    pgs_frame frameInfo{};
    uint64_t frameSerial = 0u;

    pgs_stats stats{};
};

namespace
{
    // Copies a finished (or, with wait, the newest) readback into context->frame. Returns false if none is ready.
    bool harvest(pgs_context &context, bool wait)
    {
        int newest = -1;
        for (uint32_t i = 0; i < pgs_context::kReadbacks; ++i)
        {
            if (!context.readbacks[i].pending)
                continue;
            if (newest < 0 || context.readbackSerials[i] > context.readbackSerials[newest])
                newest = static_cast<int>(i);
        }
        // The newest finished one; older pending ones are dropped once a newer one is taken.
        for (int attempt = 0; attempt < static_cast<int>(pgs_context::kReadbacks) && newest >= 0; ++attempt)
        {
            Readback &readback = context.readbacks[newest];
            const bool done = wait ? (readback.fence->wait(), true) : readback.fence->wait_timeout(0);
            if (done)
            {
                const size_t bytes = static_cast<size_t>(readback.width) * readback.height * 4u;
                const auto *mapped = static_cast<const uint8_t *>(
                    context.device.map_host_buffer(*readback.buffer, Vulkan::MEMORY_ACCESS_READ_BIT));
                context.frame.resize(bytes);
                if (mapped != nullptr)
                    std::memcpy(context.frame.data(), mapped, bytes);
                context.device.unmap_host_buffer(*readback.buffer, Vulkan::MEMORY_ACCESS_READ_BIT);
                const uint64_t serial = context.readbackSerials[newest];
                for (uint32_t i = 0; i < pgs_context::kReadbacks; ++i)
                    if (context.readbacks[i].pending && context.readbackSerials[i] <= serial)
                    {
                        context.readbacks[i].pending = false;
                        context.readbacks[i].fence.reset();
                    }
                context.frameInfo.rgba = context.frame.data();
                context.frameInfo.width = readback.width;
                context.frameInfo.height = readback.height;
                context.frameInfo.mode_width = readback.modeWidth;
                context.frameInfo.mode_height = readback.modeHeight;
                context.frameInfo.interlaced = readback.interlaced ? 1u : 0u;
                context.frameInfo.circuits = readback.circuits;
                context.frameInfo.composed = readback.composed ? 1u : 0u;
                context.frameInfo.scanout = serial;
                context.frameInfo.scale = readback.scale;
                context.frameInfo.super_sampling = readback.superSampling;
                context.frameInfo.serial = ++context.frameSerial;
                ++context.stats.frames;
                return true;
            }
            // Not finished: try the next older pending one.
            int older = -1;
            for (uint32_t i = 0; i < pgs_context::kReadbacks; ++i)
                if (context.readbacks[i].pending && context.readbackSerials[i] < context.readbackSerials[newest] &&
                    (older < 0 || context.readbackSerials[i] > context.readbackSerials[older]))
                    older = static_cast<int>(i);
            newest = older;
        }
        return false;
    }

    // Scans the display out (each circuit alone with PGS_SCANOUT_SPLIT_CIRCUITS or compose) and starts reading back
    // the image(s), or with compose the frame merged from them on the GPU. Returns 1 if there was an image.
    int scanoutAndRead(pgs_context &context, const pgs_priv_regs &regs, uint32_t field, uint32_t scanout_flags,
                       const pgs_compose_info *compose)
    {
        ++context.stats.vsyncs;

        // Each field holds the low bits of the register (SMODE2, for one, is 32 bits wide here).
        PrivRegisterState &priv = context.gs.get_priv_register_state();
        const auto set = [](auto &target, uint64_t value) { std::memcpy(&target, &value, sizeof(target)); };
        set(priv.pmode, regs.pmode);
        set(priv.smode1, regs.smode1);
        set(priv.smode2, regs.smode2);
        set(priv.dispfb1, regs.dispfb1);
        set(priv.display1, regs.display1);
        set(priv.dispfb2, regs.dispfb2);
        set(priv.display2, regs.display2);
        set(priv.extbuf, regs.extbuf);
        set(priv.extdata, regs.extdata);
        set(priv.extwrite, regs.extwrite);
        set(priv.bgcolor, regs.bgcolor);

        VSyncInfo info = {};
        info.phase = field & 1u;
        info.dst_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        info.dst_stage = VK_PIPELINE_STAGE_2_COPY_BIT;
        info.dst_access = VK_ACCESS_2_TRANSFER_READ_BIT;
        info.force_progressive = (scanout_flags & PGS_SCANOUT_FORCE_PROGRESSIVE) != 0u;
        info.overscan = (scanout_flags & PGS_SCANOUT_OVERSCAN) != 0u;
        info.anti_blur = (scanout_flags & PGS_SCANOUT_ANTI_BLUR) != 0u;
        info.crtc_offsets = (scanout_flags & PGS_SCANOUT_CRTC_OFFSETS) != 0u;
        info.adapt_to_internal_horizontal_resolution = (scanout_flags & PGS_SCANOUT_ADAPT_HORIZONTAL) != 0u;
        info.high_resolution_scanout = (scanout_flags & PGS_SCANOUT_HIGH_RES) != 0u;

        // The draws so far are submitted first (vsync expects it); the scanouts are recorded in the renderer's command
        // buffer after them and submitted together, before the copies below.
        context.gs.flush();
        const auto scan = [&]() { return context.gs.vsync(info); };
        ScanoutResult results[2] = {};
        uint32_t count = 0u;
        uint32_t circuits = 0u;
        const bool split = compose != nullptr || (scanout_flags & PGS_SCANOUT_SPLIT_CIRCUITS) != 0u;
        const uint32_t wanted = compose != nullptr ? compose->circuits : static_cast<uint32_t>(regs.pmode & 3u);
        if (split)
        {
            for (uint32_t circuit = 0; circuit < 2u; ++circuit)
            {
                if (((wanted >> circuit) & 1u) == 0u)
                    continue;
                // ENn alone; circuit 1 with MMOD 1 and ALP 0xFF, circuit 2 with SLBG 0: copied as they are.
                const uint64_t alone = (1ull << circuit) | (circuit == 0u ? (1ull << 5u) | (0xFFull << 8u) : 0ull);
                set(priv.pmode, alone);
                results[count] = scan();
                // Stacked images (no compose) share one row length.
                if (!results[count].image || (compose == nullptr && count != 0u &&
                                               results[count].image->get_width() != results[0].image->get_width()))
                    break;
                circuits |= 1u << circuit;
                ++count;
            }
            set(priv.pmode, regs.pmode);
        }
        else
        {
            results[0] = scan();
            count = results[0].image ? 1u : 0u;
        }
        context.gs.flush();
        if (count == 0u || (split && circuits != wanted))
            return 0;
        if (compose != nullptr && context.compose == nullptr)
        {
            Vulkan::ResourceLayout layout;
            layout.sets[0].storage_buffer_mask = 3u;
            layout.sets[0].meta[0].array_size = 1u;
            layout.sets[0].meta[1].array_size = 1u;
            layout.push_constant_size = sizeof(ComposeRegisters);
            context.compose = context.device.request_program(kComposeCircuitsSpirv, sizeof(kComposeCircuitsSpirv), &layout);
            if (context.compose == nullptr)
                return 0;
        }

        const ScanoutResult &result = results[0];
        // A high resolution scanout (PGS_SCANOUT_HIGH_RES, honoured by paraLLEl-GS only at 4x or more) doubles each
        // circuit image on both axes, so the compose sizes, given in scanout pixels, double too.
        const uint32_t scale = result.high_resolution_scanout ? 2u : 1u;
        uint32_t width = result.image->get_width();
        uint32_t height = 0u;
        for (uint32_t i = 0; i < count; ++i)
            height += results[i].image->get_height();
        if (compose != nullptr)
        {
            width = compose->width * scale;
            height = compose->height * scale;
        }
        Readback &readback = context.readbacks[context.nextReadback];
        context.readbackSerials[context.nextReadback] = ++context.readbackSerial;
        context.nextReadback = (context.nextReadback + 1u) % pgs_context::kReadbacks;
        if (readback.pending)
            readback.fence->wait(); // three scanouts in flight: let the oldest finish before reusing its buffers
        const VkDeviceSize bytes = VkDeviceSize(width) * height * 4u;
        if (!readback.buffer || readback.buffer->get_create_info().size < bytes)
        {
            Vulkan::BufferCreateInfo bufferInfo = {};
            bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bufferInfo.domain = Vulkan::BufferDomain::CachedHost;
            bufferInfo.size = bytes;
            readback.buffer = context.device.create_buffer(bufferInfo);
        }

        auto cmd = context.device.request_command_buffer();
        if (compose == nullptr)
        {
            VkDeviceSize offset = 0u;
            for (uint32_t i = 0; i < count; ++i)
            {
                const Vulkan::Image &image = *results[i].image;
                cmd->copy_image_to_buffer(*readback.buffer, image, offset, {}, {width, image.get_height(), 1}, 0, 0,
                                          {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
                offset += VkDeviceSize(width) * image.get_height() * 4u;
            }
            cmd->barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         VK_ACCESS_HOST_READ_BIT);
        }
        else
        {
            // The circuit images into one GPU buffer, then compose_circuits.comp writes the frame to the host buffer.
            ComposeRegisters push = {};
            push.outWidth = width;
            push.outHeight = height;
            push.flags = compose->circuits | ((compose->ct16 & 3u) << 2u) | (((regs.pmode >> 5u) & 1u) << 4u) |
                         (((regs.pmode >> 7u) & 1u) << 5u);
            push.alp = static_cast<uint32_t>((regs.pmode >> 8u) & 0xFFu);
            push.bgcolor = static_cast<uint32_t>(regs.bgcolor & 0xFFFFFFu);
            uint32_t *offsets[2] = {&push.offset1, &push.offset2};
            uint32_t *imageWidths[2] = {&push.imageWidth1, &push.imageWidth2};
            uint32_t *imageHeights[2] = {&push.imageHeight1, &push.imageHeight2};
            push.width1 = compose->circuit_width[0] * scale;
            push.height1 = compose->circuit_height[0] * scale;
            push.width2 = compose->circuit_width[1] * scale;
            push.height2 = compose->circuit_height[1] * scale;
            VkDeviceSize pixels = 0u;
            for (uint32_t i = 0, circuit = 0; i < count; ++i, ++circuit)
            {
                while (((circuits >> circuit) & 1u) == 0u)
                    ++circuit;
                *offsets[circuit] = static_cast<uint32_t>(pixels);
                *imageWidths[circuit] = results[i].image->get_width();
                *imageHeights[circuit] = results[i].image->get_height();
                pixels += VkDeviceSize(results[i].image->get_width()) * results[i].image->get_height();
            }
            // The frame is composed in GPU memory after the images and copied to the host buffer by a transfer:
            // the shader writing host memory directly over PCIe is slower and loads the CPU's memory.
            const VkDeviceSize frameOffset = (pixels * 4u + 255u) & ~VkDeviceSize(255u);
            if (!readback.images || readback.images->get_create_info().size < frameOffset + bytes)
            {
                Vulkan::BufferCreateInfo bufferInfo = {};
                bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
                bufferInfo.domain = Vulkan::BufferDomain::Device;
                bufferInfo.size = frameOffset + bytes;
                readback.images = context.device.create_buffer(bufferInfo);
            }
            for (uint32_t i = 0, circuit = 0; i < count; ++i, ++circuit)
            {
                while (((circuits >> circuit) & 1u) == 0u)
                    ++circuit;
                const Vulkan::Image &image = *results[i].image;
                cmd->copy_image_to_buffer(*readback.images, image, VkDeviceSize(*offsets[circuit]) * 4u, {},
                                          {image.get_width(), image.get_height(), 1}, 0, 0,
                                          {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
            }
            cmd->barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
            cmd->set_program(context.compose);
            cmd->set_storage_buffer(0, 0, *readback.images, 0u, pixels * 4u);
            cmd->set_storage_buffer(0, 1, *readback.images, frameOffset, bytes);
            cmd->push_constants(&push, 0, sizeof(push));
            cmd->dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
            cmd->barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
            cmd->copy_buffer(*readback.buffer, 0u, *readback.images, frameOffset, bytes);
            cmd->barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         VK_ACCESS_HOST_READ_BIT);
        }
        readback.fence.reset();
        context.device.submit(cmd, &readback.fence);
        readback.width = width;
        readback.height = height;
        readback.modeWidth = result.mode_width;
        readback.modeHeight = result.mode_height;
        readback.interlaced = result.interlaced;
        readback.circuits = circuits;
        readback.scale = scale;
        readback.superSampling = context.superSampling;
        readback.composed = compose != nullptr;
        readback.pending = true;
        return 1;
    }
}

extern "C"
{
    PGS_API uint32_t pgs_api_version(void)
    {
        return PGS_PUENTE_API_VERSION;
    }

    PGS_API pgs_context *pgs_create(const pgs_create_info *info, char *error, size_t error_size)
    {
        copyError(error, error_size, "");
        if (!Vulkan::Context::init_loader(nullptr))
        {
            copyError(error, error_size, "no Vulkan loader (vulkan-1.dll)");
            return nullptr;
        }

        auto context = std::make_unique<pgs_context>();
        context->vulkan.set_num_thread_indices(1);
        if (!context->vulkan.init_instance_and_device(nullptr, 0, nullptr, 0,
                                                      Vulkan::CONTEXT_CREATION_ENABLE_PUSH_DESCRIPTOR_BIT |
                                                          Vulkan::CONTEXT_CREATION_ENABLE_DESCRIPTOR_HEAP_BIT |
                                                          Vulkan::CONTEXT_CREATION_ENABLE_DESCRIPTOR_BUFFER_BIT))
        {
            copyError(error, error_size, "no Vulkan device");
            return nullptr;
        }
        context->device.set_context(context->vulkan);
        context->identity = identityOf(context->device.get_gpu_properties());
        // Granite only makes a pipeline cache of its own through its filesystem, which the standalone build has not:
        // without one every pipeline is compiled from scratch at each run (the driver's own disk cache aside). It has
        // to exist before paraLLEl-GS starts (its init precompiles shaders on other threads) and before any command
        // buffer, which takes the cache when it is requested. The caller's data only when saved on this very device.
        const bool withCache = info != nullptr && info->struct_size >= sizeof(pgs_create_info) &&
                               info->pipeline_cache != nullptr && info->pipeline_cache_size != 0u &&
                               sameDevice(info->pipeline_cache_device, context->identity);
        context->pipelineCache = context->device.init_pipeline_cache(
            withCache ? static_cast<const uint8_t *>(info->pipeline_cache) : nullptr,
            withCache ? info->pipeline_cache_size : 0u);
        // paraLLEl-GS starts a frame context at every submission (several per presented frame) and Granite waits for
        // the GPU work of the one it reuses: with 4 the merged scanout (pgs_vsync_composed) waited ~0.3 ms per frame
        // in a race, with 8 nothing.
        context->device.init_frame_contexts(8);
        context->deviceName = context->device.get_gpu_properties().deviceName;

        GSOptions options = {};
        const uint32_t supported = maxSupportedSuperSampling(context->device);
        if (info != nullptr && info->struct_size >= sizeof(pgs_create_info))
        {
            options.super_sampling = superSamplingOf(std::min(info->super_sampling, supported));
            options.dynamic_super_sampling = (info->flags & PGS_CREATE_DYNAMIC_SUPER_SAMPLING) != 0u;
            options.super_sampled_textures = (info->flags & PGS_CREATE_SUPER_SAMPLED_TEXTURES) != 0u;
        }
        if (!context->gs.init(&context->device, options))
        {
            copyError(error, error_size, "paraLLEl-GS init failed");
            return nullptr;
        }
        context->superSampling = static_cast<uint32_t>(options.super_sampling);
        context->maxSuperSampling = options.dynamic_super_sampling ? supported : context->superSampling;
        return context.release();
    }

    PGS_API void pgs_destroy(pgs_context *context)
    {
        if (context == nullptr)
            return;
        context->gs.flush();
        context->device.wait_idle();
        for (Readback &readback : context->readbacks)
        {
            readback.fence.reset();
            readback.buffer.reset();
            readback.images.reset();
        }
        delete context;
    }

    PGS_API const char *pgs_device_name(pgs_context *context)
    {
        return context != nullptr ? context->deviceName.c_str() : "";
    }

    PGS_API void pgs_get_device_identity(pgs_context *context, pgs_device_identity *identity)
    {
        if (identity == nullptr)
            return;
        *identity = context != nullptr ? context->identity : pgs_device_identity{};
    }

    PGS_API size_t pgs_get_pipeline_cache_size(pgs_context *context)
    {
        if (context == nullptr || !context->pipelineCache)
            return 0u;
        return context->device.get_pipeline_cache_size();
    }

    PGS_API const void *pgs_get_pipeline_cache(pgs_context *context, size_t *size)
    {
        if (size != nullptr)
            *size = 0u;
        if (context == nullptr || size == nullptr || !context->pipelineCache)
            return nullptr;
        // If the cache grows between the two queries, vkGetPipelineCacheData is incomplete and Granite fails: again.
        for (int attempt = 0; attempt < 64; ++attempt)
        {
            const size_t needed = context->device.get_pipeline_cache_size();
            if (needed == 0u)
                return nullptr;
            context->pipelineCacheCopy.resize(needed);
            if (context->device.get_pipeline_cache_data(context->pipelineCacheCopy.data(), needed))
            {
                *size = needed;
                return context->pipelineCacheCopy.data();
            }
        }
        return nullptr;
    }

    PGS_API void pgs_gif_transfer(pgs_context *context, uint32_t path, const void *data, size_t size_bytes)
    {
        if (context == nullptr || data == nullptr || path > 3u)
            return;
        size_bytes &= ~static_cast<size_t>(15u);
        if (size_bytes == 0u)
            return;
        ++context->stats.gif_transfers;
        context->stats.gif_bytes += size_bytes;
        context->gs.gif_transfer(path, data, size_bytes);
    }

    PGS_API void pgs_write_register(pgs_context *context, uint32_t address, uint64_t value)
    {
        if (context == nullptr || address > 0x7Fu)
            return;
        context->gs.write_register(static_cast<RegisterAddr>(address), value);
    }

    PGS_API void pgs_reset_gif_path(pgs_context *context, uint32_t path)
    {
        if (context == nullptr || path > 3u)
            return;
        context->gs.get_gif_path(path) = {};
    }

    PGS_API void pgs_flush(pgs_context *context)
    {
        if (context != nullptr)
            context->gs.flush();
    }

    PGS_API const void *pgs_map_vram_read(pgs_context *context, size_t offset, size_t size)
    {
        if (context == nullptr || size == 0u || offset + size > PGS_VRAM_SIZE)
            return nullptr;
        ++context->stats.vram_maps;
        return context->gs.map_vram_read(offset, size);
    }

    PGS_API void *pgs_map_vram_write(pgs_context *context, size_t offset, size_t size)
    {
        if (context == nullptr || size == 0u || offset + size > PGS_VRAM_SIZE)
            return nullptr;
        ++context->stats.vram_maps;
        return context->gs.map_vram_write(offset, size);
    }

    PGS_API void pgs_end_vram_write(pgs_context *context, size_t offset, size_t size)
    {
        if (context == nullptr || size == 0u || offset + size > PGS_VRAM_SIZE)
            return;
        context->gs.end_vram_write(offset, size);
    }

    PGS_API void pgs_read_transfer_fifo(pgs_context *context, void *data, uint32_t num_128b_words)
    {
        if (context == nullptr || data == nullptr || num_128b_words == 0u)
            return;
        context->stats.fifo_bytes += static_cast<uint64_t>(num_128b_words) * 16u;
        context->gs.read_transfer_fifo(data, num_128b_words);
    }

    PGS_API int pgs_vsync(pgs_context *context, const pgs_priv_regs *regs, uint32_t field, uint32_t scanout_flags)
    {
        if (context == nullptr || regs == nullptr)
            return 0;
        return scanoutAndRead(*context, *regs, field, scanout_flags, nullptr);
    }

    PGS_API int pgs_vsync_composed(pgs_context *context, const pgs_priv_regs *regs, uint32_t field, uint32_t scanout_flags,
                                   const pgs_compose_info *compose)
    {
        if (context == nullptr || regs == nullptr || compose == nullptr || compose->struct_size < sizeof(pgs_compose_info))
            return 0;
        if (compose->circuits == 0u || (compose->circuits & ~static_cast<uint32_t>(regs->pmode & 3u)) != 0u ||
            compose->width == 0u || compose->height == 0u || compose->width > 2048u || compose->height > 2048u)
            return 0;
        return scanoutAndRead(*context, *regs, field, scanout_flags, compose);
    }

    PGS_API int pgs_get_frame(pgs_context *context, int wait, pgs_frame *frame)
    {
        if (context == nullptr || frame == nullptr)
            return 0;
        if (!harvest(*context, wait != 0))
            return 0;
        *frame = context->frameInfo;
        return 1;
    }

    PGS_API void pgs_get_stats(pgs_context *context, pgs_stats *stats)
    {
        if (context != nullptr && stats != nullptr)
            *stats = context->stats;
    }

    PGS_API uint32_t pgs_set_quality(pgs_context *context, uint32_t super_sampling, uint32_t flags)
    {
        if (context == nullptr)
            return 0u;
        // Local memory only has room for the super samples of maxSuperSampling (GSRenderer::init_vram).
        const SuperSampling rate = superSamplingOf(std::min(super_sampling, context->maxSuperSampling));
        context->gs.flush();
        context->gs.set_super_sampling_rate(rate, true, (flags & PGS_CREATE_SUPER_SAMPLED_TEXTURES) != 0u);
        context->superSampling = static_cast<uint32_t>(rate);
        return context->superSampling;
    }

    PGS_API uint32_t pgs_get_super_sampling(pgs_context *context)
    {
        return context != nullptr ? context->superSampling : 0u;
    }
}
