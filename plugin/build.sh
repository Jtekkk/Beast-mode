#!/bin/sh
# Concatenate the shared math prelude ahead of each program to make a runnable
# .bst. Beast's `import` is reserved for a future version, so this is the
# current stand-in for a module system.
set -e
HERE=$(dirname "$0")
SRC="$HERE/src"
OUT="$HERE/build"
mkdir -p "$OUT"
for name in compressor gen_signal analyze plot math_check; do
    cat "$SRC/prelude.bst" "$SRC/$name.bst" > "$OUT/$name.bst"
done
echo "built: $OUT/{compressor,gen_signal,analyze,plot}.bst"
