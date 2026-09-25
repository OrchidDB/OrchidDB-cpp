#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DUCKDB_ROOT=${DUCKDB_ROOT:-"$ROOT/.test-deps/duckdb-1.5.2"}
case "$(uname -s)-$(uname -m)" in
  Darwin-*) ASSET=osx-universal; LIBRARY=libduckdb.dylib ;;
  Linux-x86_64) ASSET=linux-amd64; LIBRARY=libduckdb.so ;;
  Linux-aarch64) ASSET=linux-arm64; LIBRARY=libduckdb.so ;;
  *) echo 'Set up CMake and DuckDB manually on this platform; see README' >&2; exit 1 ;;
esac
if [[ ! -f "$DUCKDB_ROOT/$LIBRARY" || ! -f "$DUCKDB_ROOT/duckdb.h" ]]; then
  mkdir -p "$DUCKDB_ROOT"
  curl -fL "https://github.com/duckdb/duckdb/releases/download/v1.5.2/libduckdb-$ASSET.zip" -o "$DUCKDB_ROOT/duckdb.zip"
  unzip -qo "$DUCKDB_ROOT/duckdb.zip" -d "$DUCKDB_ROOT"
fi
cmake -S "$ROOT" -B "$ROOT/build" -DORCHIDDB_BUILD_TESTS=ON \
  -DDUCKDB_INCLUDE_DIR="$DUCKDB_ROOT" -DDUCKDB_LIBRARY="$DUCKDB_ROOT/$LIBRARY"
cmake --build "$ROOT/build"
ctest --test-dir "$ROOT/build" --output-on-failure
