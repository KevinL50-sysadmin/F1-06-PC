// pgs_readback_ring.h: the rule pgs_puente.cpp (scanoutAndRead) follows to reuse a slot of its readback ring, without
// Vulkan so that the runtime's tests (ps2xTest, ps2_gs_gpu_tests.cpp) check it.
#ifndef PGS_READBACK_RING_H
#define PGS_READBACK_RING_H

#include <stdint.h>

namespace pgs_readback
{
    // Scanout readbacks in flight at most. With 3 and a GPU ~50 ms behind (a PC with a screen capture running, issue
    // #1), each scanout waited ~18 ms for the oldest one; 4 leave room for that latency at 50 scanouts per second.
    constexpr uint32_t kSlots = 4u;
    // Before T4 the ring had 3 slots: the previous mode (PGS_SCANOUT_WAIT_OLDEST_READBACK, A/B 185) uses only the
    // first 3 so that it waits exactly as before, for the oldest of 3 readbacks in flight.
    constexpr uint32_t kPreviousSlots = 3u;

    constexpr uint32_t slotsInUse(bool waitForOldest)
    {
        return waitForOldest ? kPreviousSlots : kSlots;
    }

    // The slot the next readback takes: next as kept, or 0 if the previous mode left it out (switched at slot 3).
    constexpr uint32_t slotToUse(uint32_t next, bool waitForOldest)
    {
        return next < slotsInUse(waitForOldest) ? next : 0u;
    }

    // The slot after slot, in the slots the mode uses.
    constexpr uint32_t nextSlot(uint32_t slot, bool waitForOldest)
    {
        return (slot + 1u) % slotsInUse(waitForOldest);
    }

    enum class SlotAction
    {
        Use,            // the slot is free
        HarvestThenUse, // its readback is finished: taken as the newest frame (pgs_get_frame) before the slot is reused
        WaitThenUse,    // wait for its fence and reuse the slot without harvesting it (before T4; A/B mode 185)
        Skip,           // its readback is not finished: this scanout is not read back, nothing waits for the GPU
    };

    // The slot of the oldest readback started, before a new one reuses it. pending: still in use; finished: its fence
    // has signalled; waitForOldest: PGS_SCANOUT_WAIT_OLDEST_READBACK (the previous behaviour).
    constexpr SlotAction slotAction(bool pending, bool finished, bool waitForOldest)
    {
        if (!pending)
            return SlotAction::Use;
        if (waitForOldest)
            return SlotAction::WaitThenUse;
        return finished ? SlotAction::HarvestThenUse : SlotAction::Skip;
    }
}

#endif
