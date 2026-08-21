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
PROGRAMS := fs_atomic_test framerate_test gfx_bench

CFLAGS := -O2 -g -Wall -Wextra -Wno-unused-parameter -std=gnu11 \
          -D_LANGUAGE_C -DVERSION_US=1 -DF3DEX_GBI_2E=1 -DNON_MATCHING=1 \
          -DAVOID_UB=1 -D_FINALROM=1 -DTARGET_LINUX -DNO_SEGMENTED_MEMORY \
          -DUSE_SYSTEM_MALLOC -DENABLE_GFX_DUMMY -DWIDESCREEN -DHIGH_FPS_PC=1 \
          -fno-strict-aliasing -fwrapv \
          -I$(ROOT)/include -I$(ROOT)/src -I$(ROOT) -I$(ROOT)/src/pc -I$(ROOT)/src/pc/gfx

all: $(PROGRAMS)

fs_atomic_test: fs_atomic_test.c $(ROOT)/src/pc/fs.c
	$(CC) $(CFLAGS) -o $@ $^

framerate_test: framerate_test.c $(ROOT)/src/pc/framerate.c $(ROOT)/src/pc/configfile.c $(ROOT)/src/pc/fs.c
	$(CC) $(CFLAGS) -o $@ $^

gfx_bench: gfx_bench.c $(ROOT)/src/pc/gfx/gfx_pc.c $(ROOT)/src/pc/gfx/gfx_dummy.c \
           $(ROOT)/src/pc/gfx/gfx_cc.c $(ROOT)/src/pc/configfile.c $(ROOT)/src/pc/fs.c
	$(CC) $(CFLAGS) -o $@ $^ -lm

check: fs_atomic_test framerate_test
	./fs_atomic_test
	@echo
	./framerate_test

clean:
	$(RM) $(PROGRAMS) porttest_save.bin porttest_save.bin.tmp

.PHONY: all check clean
