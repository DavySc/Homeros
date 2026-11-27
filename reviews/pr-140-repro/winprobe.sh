#!/usr/bin/env bash
# (needs stb_image.h and stb_image_resize.h, libstb-dev's, and a wallpaper.png beside it; the
# workflow that ran it is windows-job.yml)
# Review of PR 140: builds the fonts' libraries with zig as
# src/font/windows.nix does (no nix on the Windows runner), then the PR's
# frames probe against the PR's backend and against main's (GDI), this
# review's fontprobe and the PR's test_win_font, and runs them on Windows
# itself.  BASE: the commit of main the PR is on.
set -eu
cd "$(dirname "$0")/../.."
R=reviews/pr-140-repro
BASE=${BASE:-10d8b65ebc65ca4b55ae6a8fd54b60563b42011b}
pick() { if ls src/$1/*.c >/dev/null 2>&1; then echo src/$1/*.c; else echo ref/$1.c; fi; }
mkdir -p build/win build/font
Z="zig cc -target x86_64-windows-gnu"
if [ ! -s build/font/windows.lib ]; then (
	cd build
	curl -fsSL --retry 3 -o ft.tar.xz https://download.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.xz ||
	  curl -fsSL --retry 3 -o ft.tar.xz https://downloads.sourceforge.net/project/freetype/freetype2/2.14.3/freetype-2.14.3.tar.xz
	curl -fsSL --retry 3 -o hb.tar.xz https://github.com/harfbuzz/harfbuzz/releases/download/14.5.1/harfbuzz-14.5.1.tar.xz
	sha256sum ft.tar.xz hb.tar.xz
	tar xf ft.tar.xz && tar xf hb.tar.xz
	T="-target x86_64-windows-gnu -O2"; F=freetype-2.14.3
	for m in base/ftsystem base/ftinit base/ftdebug base/ftbase base/ftbitmap base/ftglyph base/ftmm base/ftbbox \
	         gzip/ftgzip autofit/autofit truetype/truetype cff/cff psaux/psaux pshinter/pshinter psnames/psnames sfnt/sfnt smooth/smooth; do
		zig cc $T -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H="modules.h"' -I../src/font/win -I$F/include -c $F/src/$m.c -o ft_$(basename $m).o
	done
	zig c++ $T -fno-exceptions -fno-rtti -DHB_NO_MT -c harfbuzz-14.5.1/src/harfbuzz.cc -o hb.o
	zig ar rcs font/windows.lib ft_*.o hb.o
	mkdir -p font/include/harfbuzz && cp -r $F/include/* font/include/ && cp harfbuzz-14.5.1/src/hb*.h font/include/harfbuzz/
) fi
. src/font/win.sh
printf '#include "stb_image.h"\n#include "stb_image_resize.h"\n' | $Z -O2 -x c -c -I$R \
  -DSTB_IMAGE_IMPLEMENTATION -DSTBI_ONLY_PNG -DSTBI_ONLY_JPEG -DSTBI_NO_STDIO -DSTB_IMAGE_RESIZE_IMPLEMENTATION -o build/win/stb.o -
rest="$(pick grid) $(pick uni) $(pick theme) -I$R src/image/image.c src/file/*.c src/loop/*.c $(pick text)"
libs="-luser32 -lgdi32 -ldwmapi -lshell32"
$Z -std=c17 -Iinclude -O2 -o build/win/frames_new.exe src/win/probe/frames.c src/win/*.c src/paint/*.c src/font/*.c $rest build/win/stb.o $FONT_WIN $libs
$Z -std=c17 -Iinclude -O2 -o build/win/sizeprobe.exe $R/sizeprobe.c src/win/*.c src/paint/*.c src/font/*.c $rest build/win/stb.o $FONT_WIN $libs
$Z -std=c17 -Iinclude -O2 -o build/win/fontprobe.exe $R/fontprobe.c src/font/*.c $(pick uni) $FONT_WIN
$Z -std=c17 -Wall -Wextra -Werror -Iinclude -O1 -g -fsanitize=undefined -fsanitize-trap=undefined \
  -o build/win/test_win_font.exe tests/test_win_font.c src/win/*.c src/paint/*.c src/font/*.c $rest build/win/stb.o $FONT_WIN $libs
$Z -std=c17 -Wall -Wextra -Werror -Iinclude -O1 -g -fsanitize=undefined -fsanitize-trap=undefined \
  -o build/win/test_win.exe tests/test_win.c src/win/*.c src/paint/*.c src/font/*.c $rest build/win/stb.o $FONT_WIN $libs
$Z -std=c17 -Iinclude -O2 -o build/win/fill.exe $R/fill.c src/font/*.c $(pick uni) $FONT_WIN
$Z -std=c17 -O2 -o build/win/blobprobe.exe $R/blobprobe.c $FONT_WIN
$Z -o build/win/gdiface.exe $R/gdiface.c -lgdi32
if [ ! -d build/base ]; then git fetch -q --depth 1 origin $BASE && git worktree add -f --detach build/base FETCH_HEAD >/dev/null; fi
( cd build/base && $Z -std=c17 -Iinclude -O2 -o ../win/frames_old.exe ../../src/win/probe/frames.c src/win/*.c $(pick grid) $(pick uni) $(pick theme) \
    -I../../$R src/image/image.c src/file/*.c src/loop/*.c $(pick text) ../win/stb.o $libs )
run=${RUN:-}
mkdir -p build/win/wall/homeros && cp $R/wallpaper.png build/win/wall/homeros/wallpaper
if [ -z "${SIZES_ONLY:-}" ]; then
$run build/win/gdiface.exe
echo "== fontprobe, first process"; $run build/win/fontprobe.exe
echo "== fontprobe, second process"; $run build/win/fontprobe.exe | head -8
echo "== fontprobe, no family installed"; $run build/win/fontprobe.exe "No Such Family" | head -8
echo "== fill"; $run build/win/fill.exe && echo "fill: every later character still draws" || echo "fill: exit $?"
echo "== blobprobe"; $run build/win/blobprobe.exe 'C:\Windows\Fonts\msgothic.ttc' 'C:\Windows\Fonts\simsun.ttc' 'C:\Windows\Fonts\malgun.ttf' 'C:\Windows\Fonts\YuGothM.ttc' 'C:\Windows\Fonts\seguiemj.ttf' || true
echo "== test_win_font"; $run build/win/test_win_font.exe && echo "test_win_font: passed" || echo "test_win_font: FAILED ($?)"
echo "== test_win"; $run build/win/test_win.exe && echo "test_win: passed" || echo "test_win: FAILED ($?)"
mkdir -p build/win/wall/homeros && cp $R/wallpaper.png build/win/wall/homeros/wallpaper
for i in 1 2 3 4 5; do
	for v in old new; do echo "== frames $v, run $i"; $run build/win/frames_$v.exe 200 || echo "frames $v: failed ($?)"; done
	echo "== frames new over a wallpaper, run $i"; XDG_CONFIG_HOME=build/win/wall $run build/win/frames_new.exe 200 || echo "frames new: failed ($?)"
done
fi
echo "== sizes over a wallpaper"; $run build/win/sizeprobe.exe 'build\win\wall' || echo "sizeprobe: failed ($?)"
echo "== sizes without"; $run build/win/sizeprobe.exe | head -14
