# Package 21: stb_image (PNG, JPEG) and stb_image_resize from the
# distribution (libstb-dev), compiled outside our warnings and sanitizers:
# third-party code.  image.c decodes from memory (file_load), so no stdio.
# build/win/stb.o is the Windows twin, for src/win/check.sh.
STB = /usr/include/stb
STB_DEFS = -DSTB_IMAGE_IMPLEMENTATION -DSTBI_ONLY_PNG -DSTBI_ONLY_JPEG -DSTBI_NO_STDIO \
  -DSTB_IMAGE_RESIZE_IMPLEMENTATION
CPPFLAGS += -I$(STB)
OBJS += build/image/stb.o

build/image/stb.o build/win/stb.o: src/image/build.mk
	@mkdir -p $(@D)
	printf '#include "stb_image.h"\n#include "stb_image_resize.h"\n' | \
	  $(if $(findstring win,$@),zig cc -target x86_64-windows-gnu,$(CC)) -O2 -x c -c -I$(STB) $(STB_DEFS) -o $@ -
