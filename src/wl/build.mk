# Package 8, the Wayland window: libraries from pkg-config, and the protocol
# glue wayland-scanner writes from the distribution's XML (outside our
# warnings), for the editor and, from src/wl/test/, for its test.
WL_PKGS = wayland-client xkbcommon
WL_XML := $(shell pkg-config --variable=pkgdatadir wayland-protocols)
WL_PROTOS = stable/xdg-shell/xdg-shell unstable/text-input/text-input-unstable-v3 \
  staging/cursor-shape/cursor-shape-v1 unstable/tablet/tablet-unstable-v2 \
  staging/fractional-scale/fractional-scale-v1 stable/viewporter/viewporter \
  staging/xdg-activation/xdg-activation-v1 staging/xdg-toplevel-tag/xdg-toplevel-tag-v1
CPPFLAGS += -DHOMEROS_WL -Ibuild/wl $(shell pkg-config --cflags $(WL_PKGS))
LIBS += $(shell pkg-config --libs $(WL_PKGS))
OBJS += build/wl/protocols.o

build/wl/protocols.o: src/wl/build.mk $(wildcard src/wl/test/*.xml) | build/wl
	@set -e; for x in $(addprefix $(WL_XML)/,$(addsuffix .xml,$(WL_PROTOS))) src/wl/test/*.xml; do \
	  n=build/wl/$$(basename $$x .xml); \
	  wayland-scanner client-header $$x $$n-client-protocol.h; wayland-scanner private-code $$x $$n.c; done
	@set -e; for x in $(notdir $(WL_PROTOS)); do $(CC) -c -o build/wl/$$x.o build/wl/$$x.c; done
	ld -r -o $@ $(addprefix build/wl/,$(addsuffix .o,$(notdir $(WL_PROTOS))))

# the performance gates (src/wl/README.md): make build/wl/bench
build/wl/bench: src/wl/test/bench.c $(SRCS) $(OBJS) include/*.h
	$(CC) $(WARN) $(CPPFLAGS) -O2 -o $@ src/wl/test/bench.c $(SRCS) $(OBJS) $(LIBS)
