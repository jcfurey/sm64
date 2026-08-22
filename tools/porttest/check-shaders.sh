#!/bin/bash
# Compiles the shaders shader_gen_test generates with each backend's real
# toolchain, rather than the type rules the test itself applies.
#
# shader_gen_test proves the generated source is internally consistent. This
# proves a compiler accepts it, which is the check that would have caught the
# float3/float4 bug verbatim -- that one only ever surfaced as a runtime
# compile failure on the device.
#
#   ./check-shaders.sh [dump-dir] [per-backend-limit]
#
# Toolchains, any of which may be missing; a missing one is reported as skipped
# rather than passed over quietly:
#
#   metal          metal      (macOS + Xcode; if absent,
#                              xcodebuild -downloadComponent MetalToolchain)
#   opengl, gles   glslangValidator     (brew install glslang)
#   d3d            dxc                  (no macOS package provides it today)
#
# Each backend generates ~86k distinct shaders, so a limit is not optional --
# the default samples evenly across the whole set rather than taking the
# lowest shader ids, and says how many it left out.
#
# Resolving the compiler once matters more than it looks: `xcrun metal` spends
# about 2.5s per call re-resolving the toolchain, against 0.03s for the binary
# it finds. Over a few hundred shaders that is the difference between seconds
# and half an hour.

set -uo pipefail

DUMP_DIR=${1:-shader-dump}
LIMIT=${2:-400}
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

HERE=$(cd "$(dirname "$0")" && pwd)
cd "$HERE" || exit 1

status=0

# Compiles every file matching a glob, in parallel, and reports the tally.
# $1 label   $2 glob
# The command itself comes in through SHADER_CMD, evaluated with the file as
# $1, rather than being pasted into the xargs template: the Metal compiler
# lives on a very long cryptex path, and inlining it overflows the argument
# limit -- which xargs reports on stderr and which otherwise looks exactly
# like a clean run.
compile_set() {
    local label=$1 glob=$2
    local files n attempted bad

    files=$(ls $glob 2>/dev/null)
    n=$(printf '%s' "$files" | grep -c . || true)
    if [ "$n" -eq 0 ]; then
        printf '  %-7s nothing dumped\n' "$label"
        return
    fi

    printf '%s\n' "$files" \
        | xargs -P "$JOBS" -n 1 bash -c '
            out=$(eval "$SHADER_CMD" 2>&1) && { echo "OK"; exit 0; }
            echo "FAIL: $1"
            printf "%s\n" "$out" | sed -n "1,3p"
          ' _ > "$DUMP_DIR/$label.log" 2>&1

    # A compiler that never ran must not read as a clean run, so account for
    # every file: each one reports exactly one OK or one FAIL.
    attempted=$(grep -c -e '^OK$' -e '^FAIL: ' "$DUMP_DIR/$label.log" || true)
    bad=$(grep -c '^FAIL: ' "$DUMP_DIR/$label.log" || true)
    if [ "$attempted" -ne "$n" ]; then
        printf '  %-7s HARNESS ERROR: %d of %d files were compiled\n' "$label" "$attempted" "$n"
        head -5 "$DUMP_DIR/$label.log" | sed 's/^/          /'
        status=1
        return
    fi

    printf '  %-7s %4d compiled, %d rejected\n' "$label" "$n" "$bad"
    if [ "$bad" -gt 0 ]; then
        grep -A3 '^FAIL: ' "$DUMP_DIR/$label.log" | head -16 | sed 's/^/          /'
        status=1
    fi
}

rm -rf "$DUMP_DIR"
mkdir -p "$DUMP_DIR"/{metal,opengl,gles,d3d} || exit 1

./shader_gen_test --dump "$DUMP_DIR" "$LIMIT" || exit 1
echo

if METAL=$(xcrun --find metal 2>/dev/null) \
   && SDK=$(xcrun --sdk macosx --show-sdk-path 2>/dev/null); then
    export METAL SDK SHADER_CMD='"$METAL" -isysroot "$SDK" -c "$1" -o /dev/null'
    compile_set metal "$DUMP_DIR/metal/*.metal"
else
    echo "  metal   SKIPPED (no metal compiler: xcodebuild -downloadComponent MetalToolchain)"
fi

if GLSLANG=$(command -v glslangValidator 2>/dev/null); then
    export GLSLANG SHADER_CMD='"$GLSLANG" "$1"'
    compile_set opengl "$DUMP_DIR/opengl/*.vert $DUMP_DIR/opengl/*.frag"
    compile_set gles   "$DUMP_DIR/gles/*.vert $DUMP_DIR/gles/*.frag"
else
    echo "  opengl  SKIPPED (no glslangValidator: brew install glslang)"
    echo "  gles    SKIPPED (no glslangValidator: brew install glslang)"
fi

if DXC=$(command -v dxc 2>/dev/null); then
    export DXC SHADER_CMD='"$DXC" -T ps_6_0 -E PSMain "$1" -Fo /dev/null'
    compile_set d3d "$DUMP_DIR/d3d/*.hlsl"
else
    echo "  d3d     SKIPPED (no dxc on this host; HLSL is checked by shader_gen_test only)"
fi

rm -rf "$DUMP_DIR"
exit $status
