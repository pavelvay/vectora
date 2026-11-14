#!/usr/bin/env bash
# Local build-and-run helper for Vectora.
# Builds locally only; never tags, pushes, or uploads anything.
set -euo pipefail

cd "$(dirname "$0")"

BUILD_TYPE="Release"
MODE="run"
NO_BUILD=0
TEST_DIR=""

usage() {
    cat <<'EOF'
Usage: ./run.sh [options] [-- app arguments]

Options:
  --test [dir]   build and run headless self-tests (default dir: /tmp/vectora-test)
  --release      build Release (default)
  --debug        build Debug
  --no-build     do not build, only run the existing binary
  --clean        remove build/ before building
  -h, --help     show this help

Examples:
  ./run.sh
  ./run.sh --debug
  ./run.sh --test /tmp/vectora-self-test
  ./run.sh --no-build --open document.vct
  ./run.sh --screenshot /tmp/vectora.png
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --test)
            MODE="test"
            shift
            if [[ $# -gt 0 && "$1" != -* ]]; then
                TEST_DIR="$1"
                shift
            fi
            ;;
        --release)
            BUILD_TYPE="Release"
            shift
            ;;
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --no-build)
            NO_BUILD=1
            shift
            ;;
        --clean)
            rm -rf build
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            break
            ;;
        *)
            break
            ;;
    esac
done

if [[ $NO_BUILD -eq 0 ]]; then
    echo "==> Configuring ($BUILD_TYPE)"
    cmake -S . -B build \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

    jobs="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
    echo "==> Building with $jobs jobs"
    cmake --build build -j"$jobs"
fi

if [[ ! -x build/vectora ]]; then
    echo "Error: build/vectora not found. Run without --no-build first." >&2
    exit 1
fi

if [[ "$MODE" == "test" ]]; then
    TEST_DIR="${TEST_DIR:-/tmp/vectora-test}"
    mkdir -p "$TEST_DIR"
    echo "==> Running self-tests in $TEST_DIR"
    QT_QPA_PLATFORM=offscreen exec ./build/vectora --self-test "$TEST_DIR"
fi

echo "==> Starting Vectora"
exec ./build/vectora "$@"
