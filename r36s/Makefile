# The R36S (Linux, SDL2, GLES 2) build of the port.
#
# ARCH=armhf (the default, the port's target) is 32-bit, as the 3DS is: the
# disc's records, overlaid in place, keep the image's layout, and the
# headers' layout checks hold. ARCH=aarch64 builds but cannot load the disc
# yet (docs/R36S_PORT.md, "Why 32-bit").
#
# Run through r36s/build.sh, which runs this in the build image with
# "3DS Files" mounted at /game (make can't take its space) and r36s/ at
# /r36s. The game's sources are the 3DS build's, compiled with __3DS__ still
# defined against the compat headers in compat/include; the files listed in
# replaced.txt are the 3DS's own diagnostics and services and have R36S
# versions in platform/.

GAME     ?= /game
R36S     ?= /r36s
ARCH     ?= armhf
BUILD    ?= $(R36S)/build/$(ARCH)
TARGET   := $(BUILD)/nfshp2.$(ARCH)

ifeq ($(ARCH),armhf)
CC       := arm-linux-gnueabihf-gcc
# One build for every PortMaster system, so the instructions every 32-bit one
# has: ARMv7 with NEON, VFPv4 and divide (a Cortex-A7 on up), still scheduled
# for the R36S's A35. CPU=a35 builds with the A35's ARMv8 instructions too, which
# stop with an illegal instruction on an ARMv7 chip. Those are in functions
# with 2% of the R36S's CPU samples, a few instructions each. Objects don't
# depend on the flags: changing CPU takes a whole rebuild (r36s/build.sh -B).
CPU      ?= armv7
ifeq ($(CPU),a35)
ARCHFLAGS := -mcpu=cortex-a35 -mtune=cortex-a35 -mfpu=neon-fp-armv8 -mfloat-abi=hard -marm -funwind-tables
else
ARCHFLAGS := -march=armv7ve -mtune=cortex-a35 -mfpu=neon-vfpv4 -mfloat-abi=hard -marm -funwind-tables
endif
else ifeq ($(ARCH),aarch64)
CC       := aarch64-linux-gnu-gcc
ARCHFLAGS := -mcpu=cortex-a35 -mtune=cortex-a35
else
$(error ARCH is armhf or aarch64)
endif
PYTHON   ?= python3
SDL_PREFIX ?= /opt/r36s

REPLACED := $(shell cat $(R36S)/replaced.txt)
GAME_SRCS := $(filter-out $(addprefix $(GAME)/source/,$(REPLACED)),$(wildcard $(GAME)/source/*.c))
R36S_SRCS := $(wildcard $(R36S)/platform/*.c) $(wildcard $(R36S)/compat/src/*.c)

GEN      := $(BUILD)/gen
# geolit.v.pica is linked into geosolid's shbin, as on the 3DS (its
# Makefile's shader rule): DVLE 0 geosolid, DVLE 1 geolit.
SHBINS   := geosolid=$(GAME)/source/geosolid.v.pica,$(GAME)/source/geolit.v.pica \
            geofold=$(GAME)/source/geofold.v.pica
DATAFILES := $(wildcard $(GAME)/data/*.*)
GEN_STAMP := $(GEN)/.stamp

# -fno-pie/-no-pie: the 3DS code casts pointers to u32, so on aarch64
# everything the game points at must sit below 4 GB: the executable's own
# data (a non-PIE executable loads at 0x400000) and the game's heap
# (platform/lowheap.c). On armhf every address fits.
CFLAGS   := -std=gnu11 -O2 -g $(ARCHFLAGS) -fno-pie -fno-strict-aliasing \
            -ffunction-sections -fdata-sections -fno-common \
            -Wall -Wno-unused-function -Wno-unused-variable \
            -Wno-unused-but-set-variable -Werror=implicit-function-declaration \
            -Werror=int-conversion -Werror=incompatible-pointer-types \
            -D__3DS__ -DNFSHP2_R36S \
            -I$(R36S)/compat/include -I$(R36S)/platform -I$(GAME)/source -I$(GEN) \
            -I$(SDL_PREFIX)/include/SDL2 -D_REENTRANT -D_FILE_OFFSET_BITS=64
comma    := ,
# The game's file opens are wrapped so that "sdmc:/..." lands under the
# port's folder (platform/r36s_platform.c), and main so that the game runs
# on a stack from the heap with a guard page, as every game thread does
# (the same file). On aarch64 the malloc family is wrapped too, so that
# what the game allocates is below 4 GB (platform/lowheap.c).
WRAPS    := fopen freopen mkdir open opendir __xstat stat remove rename unlink main
ifeq ($(ARCH),armhf)
# With 64-bit file offsets (CFLAGS) glibc's headers rename these.
WRAPS    += fopen64 freopen64 open64 __xstat64
endif
ifeq ($(ARCH),aarch64)
WRAPS    += malloc free calloc realloc memalign posix_memalign aligned_alloc             malloc_usable_size
endif
LDFLAGS  := -no-pie -g -Wl,--gc-sections -static-libgcc -L$(SDL_PREFIX)/lib \
            -Wl,-rpath,'$$ORIGIN/libs.$(ARCH)' $(addprefix -Wl$(comma)--wrap=,$(WRAPS))
LDLIBS   := -lSDL2 -lGLESv2 -lm -lpthread -ldl

OBJDIR   := $(BUILD)/obj
GAME_OBJS := $(patsubst $(GAME)/source/%.c,$(OBJDIR)/game/%.o,$(GAME_SRCS))
R36S_OBJS := $(patsubst $(R36S)/%.c,$(OBJDIR)/r36s/%.o,$(R36S_SRCS))
GEN_OBJS  = $(patsubst $(GEN)/%.c,$(OBJDIR)/gen/%.o,$(wildcard $(GEN)/*.c))

.PHONY: all clean gen
all: $(TARGET)

$(GEN_STAMP): $(R36S)/scripts/gen_embedded.py $(wildcard $(GAME)/source/*.v.pica) $(DATAFILES)
	@mkdir -p $(GEN)
	$(PYTHON) $(R36S)/scripts/gen_embedded.py $(GEN) \
	    $(addprefix --shbin ,$(SHBINS)) $(addprefix --bin ,$(DATAFILES))
	@touch $@

gen: $(GEN_STAMP)

$(OBJDIR)/game/%.o: $(GAME)/source/%.c | $(GEN_STAMP)
	@mkdir -p $(dir $@)
	@echo CC $(notdir $<)
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(OBJDIR)/r36s/%.o: $(R36S)/%.c | $(GEN_STAMP)
	@mkdir -p $(dir $@)
	@echo CC r36s/$*.c
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(OBJDIR)/gen/%.o: $(GEN)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(GAME_OBJS) $(R36S_OBJS) $(GEN_STAMP)
	@$(MAKE) --no-print-directory gen-objs
	@echo LD $(notdir $@)
	@# The generated objects by a shell glob: make read gen/ before
	@# gen_embedded.py filled it, on a clean build.
	@$(CC) $(LDFLAGS) -o $@ $(GAME_OBJS) $(R36S_OBJS) $(OBJDIR)/gen/*.o $(LDLIBS)

.PHONY: gen-objs
gen-objs: $(GEN_OBJS)

clean:
	rm -rf $(BUILD)

-include $(GAME_OBJS:.o=.d) $(R36S_OBJS:.o=.d)
