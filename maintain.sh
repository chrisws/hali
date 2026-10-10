#!/bin/sh
# maintain.sh — project build & test runner
# Usage:
#   ./maintain.sh          # build + test (default)
#   ./maintain.sh build    # build only
#   ./maintain.sh test     # run tests only
#   ./maintain.sh clean    # clean build artifacts
#   ./maintain.sh help     # show this help

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
TESTS_BUILD_DIR="$SCRIPT_DIR/tests/build"

usage() {
  echo "Usage: $0 [build|test|clean|all|help]"
  echo ""
  echo "  build   Compile the main project"
  echo "  test    Build and run unit tests"
  echo "  clean   Remove build artifacts"
  echo "  all     Build project + run tests (default)"
  echo "  help    Show this message"
}

do_build() {
  echo "==> Building main project..."
  make -C "$BUILD_DIR" -j"$(nproc)"
  echo "==> Build complete."
}

do_test() {
  echo "==> Building unit tests..."
  make -C "$TESTS_BUILD_DIR" -j"$(nproc)"
  echo "==> Running unit tests..."
  ctest --test-dir "$TESTS_BUILD_DIR" --output-on-failure
  echo "==> Tests complete."
}

do_clean() {
  echo "==> Cleaning build artifacts..."
  make -C "$BUILD_DIR" clean
  make -C "$TESTS_BUILD_DIR" clean
  echo "==> Clean complete."
}

case "${1:-all}" in
  build)
    do_build
    ;;
  test)
    do_test
    ;;
  clean)
    do_clean
    ;;
  all)
    do_build
    do_test
    ;;
  help|--help|-h)
    usage
    ;;
  *)
    echo "Unknown option: $1"
    usage
    exit 1
    ;;
esac