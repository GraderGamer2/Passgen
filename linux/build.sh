#!/bin/sh
# Needs: gcc, curl. (fonttools only if assets/quicksand.ttf is missing.)
set -e; cd "$(dirname "$0")"
[ -f stb_truetype.h ] || curl -fL -o stb_truetype.h https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h
if [ ! -f assets/quicksand.ttf ]; then
  curl -fL -o /tmp/q.ttf 'https://github.com/google/fonts/raw/main/ofl/quicksand/Quicksand%5Bwght%5D.ttf'
  fonttools varLib.instancer /tmp/q.ttf wght=500 -o /tmp/q500.ttf
  pyftsubset /tmp/q500.ttf --unicodes=U+0020-007E --layout-features= --no-hinting --desubroutinize --output-file=assets/quicksand.ttf
fi
gcc -Os -Wall -Wno-unused-function -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-stack-protector -fcf-protection=none -fno-ident -fmerge-all-constants -fno-plt -ffunction-sections -fdata-sections -Wl,--gc-sections -Wl,-z,norelro -Wl,-z,noseparate-code -Wl,--build-id=none -Wl,--hash-style=gnu -s -o passgen passgen.c -lm
strip -R .comment -R .note.gnu.property -R .note.ABI-tag passgen 2>/dev/null || true
ls -l passgen assets/quicksand.ttf
