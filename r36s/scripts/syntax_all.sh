#!/bin/sh
# Syntax-checks the game sources for aarch64 against the compat headers,
# skipping the files r36s/replaced.txt lists (the R36S has its own).
# Usage (in the build image): sh r36s/scripts/syntax_all.sh [file.c ...]
cd "/src/3DS Files/source" || exit 1
FLAGS="-std=gnu11 -fsyntax-only -fno-strict-aliasing -D__3DS__ -DNFSHP2_R36S -I/src/r36s/compat/include -I/src/r36s/platform -I. -Wno-unused -Werror=implicit-function-declaration"
files="$*"; [ -z "$files" ] && files=$(ls *.c | grep -vxF -f /src/r36s/replaced.txt)
for f in $files; do
  if aarch64-linux-gnu-gcc $FLAGS "$f" 2>/tmp/e.txt; then echo "OK $f"; else echo "FAIL $f"; grep -E 'error' /tmp/e.txt | sed "s|^|  |"; fi
done
