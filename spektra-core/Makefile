# Phase 0 extraction spike build (Linux/gcc; MSVC comes later).
CC      ?= gcc
CFLAGS  ?= -std=gnu11 -O2 -g -Wall -Wextra -fopenmp
DT      := third_party/darktable
SHIMS   := shims
GLIB    := $(shell pkg-config --cflags --libs glib-2.0 json-glib-1.0)

# Phase 1 port build: the engine compiled against the embedded shims
# (no glib/json-glib), the shared chain, and the Linux reference renderer.
PORT    := $(SHIMS)/portability
SIMPORT := sim_port
PORT_SRC := $(DT)/spektra_sim.c $(DT)/spektra_core.c \
            $(SHIMS)/portability/glib_mini.c $(SHIMS)/portability/json_mini.c \
            $(SHIMS)/dt_stubs.c \
            $(SIMPORT)/sp_chain.c $(SIMPORT)/sp_color.c $(SIMPORT)/sp_params.c
PORT_CFLAGS := -std=gnu11 -O2 -g -Wall -Wextra -I$(DT) -I$(SIMPORT) -I$(SHIMS) \
               -I$(PORT)/glib-2.0 -I$(PORT)

.PHONY: all run run-port clean
all: build/spike

build/spike: spike/spike_main.c $(DT)/spektra_core.c $(DT)/spektra_sim.c shims/dt_stubs.c
	@mkdir -p build
	$(CC) $(CFLAGS) -I$(DT) -I$(SHIMS) \
	  spike/spike_main.c $(DT)/spektra_core.c $(DT)/spektra_sim.c shims/dt_stubs.c \
	  $(GLIB) -lm -o $@

build/refrender: ../tools/refrender/refrender.c $(PORT_SRC)
	@mkdir -p build
	$(CC) $(PORT_CFLAGS) ../tools/refrender/refrender.c $(PORT_SRC) -lm -o $@

run-port: build/refrender
	./build/refrender --anchor

run: build/spike
	./build/spike

clean:
	rm -rf build spike_out.ppm