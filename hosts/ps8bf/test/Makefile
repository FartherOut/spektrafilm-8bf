# Makefile — Linux mock-host harness for the ps8bf filter.
#
# Builds the *portable* host code (filter_loop/params/pix_convert) + the engine
# + sim_port, and drives them through the same selectors the Windows DLL uses.
# No Win32, no Photoshop. See run.sh for the end-to-end refrender comparison.
#
#   make            build/harness
#   make check      build + run selftest (from spektra-core/data, so "pack"
#                   resolves) and the refrender byte comparison
#   make clean
#
# Paths are relative to this directory (hosts/ps8bf/test).

CURDIR  := $(shell pwd)
REPO    := $(CURDIR)/../../..
CORE    := $(REPO)/spektra-core
DT      := $(CORE)/third_party/darktable
SIMP    := $(CORE)/sim_port
SHIMS   := $(CORE)/shims
PSRC    := $(CURDIR)/../src
PPI     := $(CURDIR)/../pi
PACK    := $(CORE)/data/pack
GEN     := $(CURDIR)/build/gen

CC      ?= cc
CFLAGS  ?= -std=gnu11 -O2 -g -Wall -Wextra
INCS    := -I$(PPI) -I$(PSRC) -I$(GEN) \
           -I$(DT) -I$(SIMP) -I$(SHIMS) \
           -I$(SHIMS)/portability -I$(SHIMS)/portability/glib-2.0

CORE_SRC := $(DT)/spektra_sim.c $(DT)/spektra_core.c \
            $(SHIMS)/portability/glib_mini.c $(SHIMS)/portability/json_mini.c \
            $(SHIMS)/dt_stubs.c $(SIMP)/sp_chain.c $(SIMP)/sp_color.c $(SIMP)/sp_params.c
HOST_SRC := $(PSRC)/filter_loop.c $(PSRC)/params.c $(PSRC)/preset.c $(PSRC)/icc_profile.c \
            $(PSRC)/pix_convert.c $(PSRC)/sf_parallel.c $(PSRC)/sf_preview.c

.PHONY: all check refrender clean

all: build/harness

build/harness: harness.c $(GEN)/pack_list.h $(CORE_SRC) $(HOST_SRC)
	@mkdir -p build
	$(CC) $(CFLAGS) $(INCS) harness.c $(CORE_SRC) $(HOST_SRC) -lm -pthread -o $@

$(GEN)/pack_list.h: $(REPO)/tools/gen_assets.py
	@mkdir -p $(GEN)
	python3 $(REPO)/tools/gen_assets.py --win "$(PACK)" "$(GEN)"

# refrender lives in spektra-core/Makefile; build it there.
refrender:
	$(MAKE) -C $(CORE) build/refrender

check: build/harness refrender
	./run.sh

clean:
	rm -rf build
