#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=${GOODIX_COVERAGE_SOURCE_DIR:-"$here/../libfprint-driver"}
build=$(mktemp -d "${TMPDIR:-/tmp}/goodix-coverage-test.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -DGOODIX_COVERAGE_TESTING -O2 -Wall -Wextra -Werror -pedantic ${CFLAGS:-} \
  -I "$src" "$here/../tests/native/test-coverage.c" "$src/goodix5e0a-coverage.c" \
  -o "$build/test-coverage" -lm ${LDFLAGS:-}
"$build/test-coverage"
