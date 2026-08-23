# Port tests and benchmarks

Standalone checks for the port's platform layer (`src/pc`). They compile the
real sources rather than copies, so they break when the behavior they cover
changes. None of them need a ROM or extracted assets, so they run on a bare
checkout.

```
make -C tools/porttest check    # build and run the tests
make -C tools/porttest check-sanitize # ASan + UBSan + structured fuzz smoke
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

Covers the frame pacing policy in `src/pc/framerate.c`: CPU-lateness and
confirmed-presentation backoff, learned-ceiling preservation across resume,
and sub-frame count selection.

On iOS, display callbacks submit one sub-frame at a time. The policy watches
both logic timing and the number of frames Core Animation confirms reached the
screen, so invisible presentation drops can lower the GPU workload before they
turn into thermal throttling. Getting a device to throttle on demand is
impractical, so the test drives the policy with synthetic timings — sustained
overload, dropped presentations, suspension, isolated hitches, and recovery.

The selection tests also preserve compatibility with old configuration files:
a legacy 90 fps cap has to divide the display's refresh multiple, so on a
120 Hz display it deliberately rounds down to 60. The user-facing menu no
longer offers that misleading choice.

## `touch_layout_test`

Covers the on-screen control layout in
`src/pc/controller/controller_touch.c`. Fixed button positions are the most
common complaint about any mobile port, so the layout is adjustable and
persisted, and the things worth pinning down are the ones that would strand
a player: that the size setting actually widens the hit area, that a drag
heading off screen is clamped back into view rather than putting a button
somewhere unreachable, and that a damaged layout file degrades to the
default instead of being applied.

It also verifies safe-area behavior across representative phones and tablets,
portrait control scaling, single ownership of the floating stick, one-shot
haptic transitions, and automatic overlay visibility when a gamepad connects.

## `viewport_layout_test`

Covers the native-drawable rectangle used for the game. Landscape stays
full-screen while publishing safe HUD margins, Retro Mode remains centered at
4:3 on wide displays, and iPhone and iPad portrait layouts fit a complete 4:3
panel below the top safe area.

## `controller_map_test`

Covers default SDL-style gamepad mappings, triggers and right-stick virtual
buttons, remapping, config round-trips, invalid saved values, D-pad menu access,
and the controller-presence state used by the touch overlay.

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

The same suite rejects malformed matrix-stack pops, light counts, vertex loads,
and triangle indices, and forces the texture cache through a complete recycle.
`gfx_command_fuzz.c` feeds those fields from arbitrary bytes while keeping all
command pointers in valid local storage.

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
