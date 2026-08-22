# Standalone tests and benchmarks for the port's platform layer (src/pc).
#
# These build against the real sources rather than copies, so they fail when
# the code they cover changes behavior. They need no ROM and no extracted
# assets, so they run anywhere the repository is checked out.
#
#   make -C tools/porttest        # build
#   make -C tools/porttest check  # build and run the tests
#   ./tools/porttest/gfx_bench 5000

ROOT := ../..
PROGRAMS := fs_atomic_test framerate_test touch_layout_test gfx_pool_test \
            shader_gen_test gfx_bench

CFLAGS := -O2 -g -Wall -Wextra -Wno-unused-parameter -std=gnu11 \
          -D_LANGUAGE_C -DVERSION_US=1 -DF3DEX_GBI_2E=1 -DNON_MATCHING=1 \
          -DAVOID_UB=1 -D_FINALROM=1 -DTARGET_LINUX -DNO_SEGMENTED_MEMORY \
          -DUSE_SYSTEM_MALLOC -DENABLE_GFX_DUMMY -DWIDESCREEN -DHIGH_FPS_PC=1 \
          -fno-strict-aliasing -fwrapv \
          -I$(ROOT)/include -I$(ROOT)/src -I$(ROOT) -I$(ROOT)/src/pc -I$(ROOT)/src/pc/gfx

# macOS host fixes. Two things a Linux host gets for free:
#   - fortified bzero() and sprintf() expand to __builtin calls that collide
#     with the plain declarations in include/PR/os_libc.h;
#   - <SDL2/SDL.h> is not on the default include path under Homebrew;
#   - Apple's SDK marks sprintf() deprecated, which the shader generator uses
#     throughout and which warns nowhere else.
ifeq ($(shell uname -s),Darwin)
  CFLAGS += -D_FORTIFY_SOURCE=0 -Wno-deprecated-declarations \
            $(shell pkg-config --cflags sdl2 2>/dev/null)
endif

CXXFLAGS := $(subst -std=gnu11,-std=gnu++17,$(CFLAGS))

all: $(PROGRAMS)

fs_atomic_test: fs_atomic_test.c $(ROOT)/src/pc/fs.c
	$(CC) $(CFLAGS) -o $@ $^

framerate_test: framerate_test.c $(ROOT)/src/pc/framerate.c $(ROOT)/src/pc/configfile.c $(ROOT)/src/pc/fs.c
	$(CC) $(CFLAGS) -o $@ $^

GFX_SRCS := $(ROOT)/src/pc/gfx/gfx_pc.c $(ROOT)/src/pc/gfx/gfx_dummy.c \
            $(ROOT)/src/pc/gfx/gfx_cc.c $(ROOT)/src/pc/configfile.c $(ROOT)/src/pc/fs.c

# TARGET_IOS selects the touch code, and ENABLE_OPENGL is left off so no
# GLES headers are needed. It is scoped to the two translation units that
# want it: fs.c under TARGET_IOS resolves paths through SDL, which this test
# has no reason to link against.
touch_layout_test.o: touch_layout_test.c
	$(CC) $(CFLAGS) -DTARGET_IOS -c -o $@ $<

controller_touch_ios.o: $(ROOT)/src/pc/controller/controller_touch.c
	$(CC) $(CFLAGS) -DTARGET_IOS -c -o $@ $<

touch_layout_test: touch_layout_test.o controller_touch_ios.o \
                   $(ROOT)/src/pc/configfile.c $(ROOT)/src/pc/fs.c
	$(CC) $(CFLAGS) -o $@ $^ -lm

gfx_pool_test: gfx_pool_test.c $(GFX_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

# Same test under AddressSanitizer, where a pool overrun becomes a report
# instead of silent memory corruption
gfx_pool_test_asan: gfx_pool_test.c $(GFX_SRCS)
	$(CC) $(CFLAGS) -fsanitize=address -fno-omit-frame-pointer -o $@ $^ -lm

# Every backend's shader generator, each behind the flag that selects it in a
# real build. They are all plain string building over a CCFeatures and pull in
# no graphics headers, so they link into one test on any host. The GLES dialect
# is the same translation unit compiled again with the entry point renamed, so
# both GLSL variants can be linked at once.
GFX := $(ROOT)/src/pc/gfx

shader_gen_test.o: shader_gen_test.cpp
	$(CXX) $(CXXFLAGS) -DENABLE_METAL -DENABLE_OPENGL -DENABLE_DX11 -c -o $@ $<

gfx_metal_shader.o: $(GFX)/gfx_metal_shader.c
	$(CC) $(CFLAGS) -DENABLE_METAL -c -o $@ $<

gfx_opengl_shader.o: $(GFX)/gfx_opengl_shader.c
	$(CC) $(CFLAGS) -DENABLE_OPENGL -c -o $@ $<

gfx_opengl_shader_gles.o: $(GFX)/gfx_opengl_shader.c
	$(CC) $(CFLAGS) -DENABLE_OPENGL -DUSE_GLES \
	    -Dgfx_opengl_generate_shader_source=gfx_opengl_generate_shader_source_gles \
	    -c -o $@ $<

gfx_direct3d_common.o: $(GFX)/gfx_direct3d_common.cpp
	$(CXX) $(CXXFLAGS) -DENABLE_DX11 -c -o $@ $<

shader_gen_test: shader_gen_test.o gfx_metal_shader.o gfx_opengl_shader.o \
                 gfx_opengl_shader_gles.o gfx_direct3d_common.o $(GFX)/gfx_cc.c
	$(CXX) $(CXXFLAGS) -o $@ $^

gfx_bench: gfx_bench.c $(ROOT)/src/pc/gfx/gfx_pc.c $(ROOT)/src/pc/gfx/gfx_dummy.c \
           $(ROOT)/src/pc/gfx/gfx_cc.c $(ROOT)/src/pc/configfile.c $(ROOT)/src/pc/fs.c
	$(CC) $(CFLAGS) -o $@ $^ -lm

check: fs_atomic_test framerate_test touch_layout_test gfx_pool_test shader_gen_test
	./fs_atomic_test
	@echo
	./framerate_test
	@echo
	./touch_layout_test
	@echo
	./gfx_pool_test
	@echo
	./shader_gen_test

# Puts the generated shaders through each backend's real compiler rather than
# the type rules shader_gen_test applies. Kept out of `check`, which needs no
# toolchain beyond a C compiler and runs anywhere. See the script for what each
# backend needs and what it does when a compiler is missing.
SHADER_DUMP_DIR := shader-dump
SHADER_LIMIT ?= 400

check-shaders: shader_gen_test
	./check-shaders.sh $(SHADER_DUMP_DIR) $(SHADER_LIMIT)

clean:
	$(RM) $(PROGRAMS) gfx_pool_test_asan *.o porttest_save.bin porttest_save.bin.tmp \
	      sm64_touch_layout.txt sm64_touch_layout.txt.tmp
	$(RM) -r $(SHADER_DUMP_DIR)

.PHONY: all check check-shaders clean
