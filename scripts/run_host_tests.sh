#!/bin/bash
# SmartAqua — host unit tests for the SmartAquaCore Arduino library.
# The library is deliberately platform-free C/C++ so this runs on any host
# with a C++ compiler, in under a second, no board attached.
#
# The Arduino toolchain compiles the .cpp files as C++ (with a C wrapper for
# the C headers), so the host build mirrors that: g++ for library sources,
# gcc for the C test file.
set -e
cd "$(dirname "$0")/.."

LIB=arduino/libraries/SmartAquaCore/src
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

g++ -Wall -Wextra -O2 -I "$LIB" -c "$LIB/saq_protocol.cpp" -o "$OUT/saq_protocol.o"
g++ -Wall -Wextra -O2 -I "$LIB" -c "$LIB/saq_schedule.cpp" -o "$OUT/saq_schedule.o"
g++ -Wall -Wextra -O2 -I "$LIB" -c "$LIB/saq_portion.cpp" -o "$OUT/saq_portion.o"
gcc -std=c99 -Wall -Wextra -O2 -I "$LIB" scripts/test_saq_core.c \
    "$OUT/saq_protocol.o" "$OUT/saq_schedule.o" "$OUT/saq_portion.o" \
    -o "$OUT/saq_test"

"$OUT/saq_test"
