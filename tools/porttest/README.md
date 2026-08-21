# Port tests and benchmarks

Standalone checks for the port's platform layer (`src/pc`). They compile the
real sources rather than copies, so they break when the behavior they cover
changes. None of them need a ROM or extracted assets, so they run on a bare
checkout.

```
make -C tools/porttest check    # build and run the tests
make -C tools/porttest          # also builds the benchmark
./tools/porttest/gfx_bench 3000
```

## `fs_atomic_test`

Covers the atomic file writing in `src/pc/fs.c`. The save file and the
config are rewritten in full whenever they change, and the system can kill a
suspended app mid-write with no warning, so the property that matters is
that an interrupted write cannot damage what is already on disk. The test
abandons a write partway through and checks the previous contents survive.

## `framerate_test`

Covers the frame pacing policy in `src/pc/framerate.c`: the adaptive
sub-frame backoff and the sub-frame count selection.

Sub-frames are rendered inside the same 1/30 s that the game logic runs in,
so a device that cannot draw them all falls behind on *logic* frames and the
game runs in slow motion rather than merely looking choppier. The policy
watches frame timings and lowers the ceiling before that happens. Getting a
device to thermally throttle on demand is impractical, so the test drives
the policy with synthetic timings — sustained overload, an isolated level
load stall, occasional hitches, and recovery.

The selection tests also pin down behavior that is easy to get wrong: a
frame cap has to divide the display's refresh multiple or vsync pacing goes
uneven, so 90 fps on a 120 Hz display deliberately rounds down to 60.

## `gfx_pool_test`

Feeds the interpreter more distinct colour combiner configurations than its
fixed pools hold.

`gfx_pc` keeps a 64-entry combiner pool, and every rendering backend keeps a
shader program pool of the same size. All of them were filled with a bare
post-increment and no bounds check. Super Mario 64 uses a fraction of the
capacity, so it never showed up in play — but a display list is *data*, and
data should not be able to write past the end of a static array. Under
AddressSanitizer the unguarded version reports a four-byte global buffer
overflow landing immediately before `gfx_texture_cache`, whose hashmap is
full of pointers.

```
make -C tools/porttest gfx_pool_test_asan && ./tools/porttest/gfx_pool_test_asan
```

Both pools now stop at their limit and reuse an existing entry, so an
over-complex display list renders with the wrong combiner instead of
corrupting memory.

## `gfx_bench`

Times the N64 display list interpreter (`src/pc/gfx/gfx_pc.c`) against a
rendering backend that does nothing, so what it reports is the interpreter's
own CPU cost: vertex transform and lighting, state diffing, combiner and
texture cache lookups, and filling the vertex buffer.

It exists because "rendering a sub-frame re-runs the entire display list"
sounds far more expensive than it measures. At a few thousand triangles one
full interpretation costs roughly 0.1 ms, so three extra sub-frames at
120 fps come to about 1% of the 33 ms logic frame. The cost of a high frame
rate in this port is on the GPU, not here.

Use it before believing any claim — including one in a commit message —
about where this port spends its CPU time. It is a relative instrument: the
absolute numbers depend on the machine, so measure before and after a change
on the same one, several runs each way.
