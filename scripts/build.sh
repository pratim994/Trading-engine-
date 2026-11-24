#!/bin/bash
# Incremental build. Usage: scripts/build.sh [--clean] [--debug] [--asan] [--all]
#   (default)  Release build into ./cmake-build-release
#   --debug    also build Debug into ./cmake-build-debug
#   --asan     also build Debug + AddressSanitizer/UBSan + libstdc++ bounds assertions into ./cmake-build-asan
#   --all      release + debug
#   --clean    clean each selected build dir first (the old script always did this)
set -euo pipefail
cd "$(dirname "$0")/.."

CMAKE=$(command -v cmake) || { echo "cmake not found" >&2; exit 2; }
GEN=(); BUILD_PROG=()
if NINJA=$(command -v ninja); then GEN=(-G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA"); fi
JOBS=${JOBS:-$(( $(nproc) > 4 ? 4 : $(nproc) ))}   # capped so a laptop is not saturated while compiling

CLEAN=0; TYPES=(release)
for a in "$@"; do
  case "$a" in
    --clean) CLEAN=1 ;;
    --debug) TYPES+=(debug) ;;
    --asan)  TYPES+=(asan) ;;
    --all)   TYPES+=(debug) ;;
    *) echo "unknown option: $a" >&2; exit 2 ;;
  esac
done

for t in "${TYPES[@]}"; do
  dir="./cmake-build-$t"; extra=()
  case "$t" in
    release) extra=(-DCMAKE_BUILD_TYPE=Release) ;;
    debug)   extra=(-DCMAKE_BUILD_TYPE=Debug) ;;
    asan)    extra=(-DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS_DEBUG="-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -D_GLIBCXX_ASSERTIONS") ;;
  esac
  echo "== configuring $t"; "$CMAKE" -DCMAKE_VERBOSE_MAKEFILE=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5 "${GEN[@]}" "${extra[@]}" -S . -B "$dir"
  [ "$CLEAN" = 1 ] && "$CMAKE" --build "$dir" --target clean
  echo "== building $t"; "$CMAKE" --build "$dir" -j "$JOBS"
done
