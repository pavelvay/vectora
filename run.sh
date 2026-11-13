#!/usr/bin/env bash
# Builds Vectora on first run, then launches it.
set -euo pipefail

cd "$(dirname "$0")"

if [ ! -x build/vectora ]; then
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j"$(nproc)"
fi

exec ./build/vectora "$@"
