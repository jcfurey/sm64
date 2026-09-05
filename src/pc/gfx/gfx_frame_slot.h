#ifndef GFX_FRAME_SLOT_H
#define GFX_FRAME_SLOT_H

#include <atomic>
#include <cstdint>

// A nonzero frame serial owns the arena; zero means it is available. Keeping
// ownership and availability in one atomic prevents a delayed completion from
// releasing an arena after another frame has acquired it.
class GfxFrameSlot {
public:
    bool try_acquire(uint64_t serial) {
        uint64_t expected = 0;
        return serial != 0 && owner.compare_exchange_strong(expected, serial);
    }

    bool release(uint64_t serial) {
        return serial != 0 && owner.compare_exchange_strong(serial, 0);
    }

private:
    std::atomic<uint64_t> owner{0};
};

#endif
