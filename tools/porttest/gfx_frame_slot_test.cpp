#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "gfx_frame_slot.h"

static void check(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
    std::printf("  ok: %s\n", message);
}

int main() {
    GfxFrameSlot slot;
    check(!slot.try_acquire(0) && !slot.release(0), "zero cannot own or release a slot");
    check(slot.try_acquire(1) && !slot.try_acquire(2), "an occupied arena cannot be acquired");
    check(!slot.release(2) && slot.release(1) && !slot.release(1),
          "only the owning frame releases the arena, once");

    // Completion and presentation can race to release the same frame.
    check(slot.try_acquire(2), "a completed arena can be reused");
    std::atomic<bool> start{false};
    std::atomic<int> releases{0};
    auto callback = [&] {
        while (!start.load()) std::this_thread::yield();
        if (slot.release(2)) releases.fetch_add(1);
    };
    std::thread completion(callback), presentation(callback);
    start.store(true);
    completion.join();
    presentation.join();
    check(releases.load() == 1, "racing callbacks release exactly once");

    // Keep delivering stale callbacks while the render thread repeatedly
    // reuses the arena. None may release a newer frame, even during acquire.
    std::atomic<bool> stop{false}, stale_release{false};
    std::thread delayed([&] {
        while (!stop.load()) {
            if (slot.release(2)) stale_release.store(true);
        }
    });
    bool intact = true;
    for (uint64_t serial = 3; serial < 100003; serial++) {
        if (!slot.try_acquire(serial) || slot.try_acquire(serial + 1) || !slot.release(serial)) {
            intact = false;
            break;
        }
    }
    stop.store(true);
    delayed.join();
    check(intact && !stale_release.load(), "stale callbacks cannot release reused arenas");
    std::puts("PASS");
}
