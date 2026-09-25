# OrchidDB C++ client

C++17 compiler binding plus move-only Arrow C stream results. Your application owns the database, connection, schema mappings, UDFs, extensions, transactions and caches. The compiler never receives result data and has no DuckDB dependency.

```cpp
#include <orchiddb/orchiddb.hpp>
orchiddb::Compiler compiler; // ORCHIDDB_NATIVE_LIBRARY, or OS shared-library path
orchiddb::Json request = /* compiler JSON v1 with query, tables and graph mappings */;
auto plan = compiler.compile(request);
auto result = engine.execute(plan); // your ExecutionEngine
const auto schema = result.schema();
while (auto batch = result.next()) {
    // ArrowArray struct, column buffers, offsets and validity bitmaps.
    consume(batch.get());
}
```

The [complete DuckDB integration example](tests/integration.cpp) and [borrowed connection adapter](examples/duckdb_engine.hpp) show real data, nulls, large integers, rollback and result ownership. DuckDB is a test/application dependency. This example uses DuckDB's Arrow query API, which materializes the query before exposing columnar batches; Arrow does not imply streaming query execution or zero-copy for all engines.

Implement `ExecutionEngine` (`id`, `dialect`, `execute`) to support another Arrow-capable backend. `ArrowResult` takes ownership of a supplied `ArrowArrayStream` and clears the source. Stream, schema and batch have independent release callbacks and move-only RAII ownership. Batches remain valid after advancing/closing the stream. Connections must outlive result production. Neither OrchidDB nor its adapters commit or close your connection. Stream callbacks are serialized by the caller; no automatic parallel execution/federation. Postgres SQL rendering is supported, but this repository only integration-tests DuckDB.

## Build and test

Get the compiler library from [OrchidDB-native](https://github.com/OrchidDB/OrchidDB-native), then:

```sh
export ORCHIDDB_NATIVE_LIBRARY=/absolute/path/liborchiddb_compiler.dylib # .so on Linux
cmake -S . -B build -DORCHIDDB_BUILD_TESTS=ON \
  -DDUCKDB_INCLUDE_DIR=/path/to/duckdb/include -DDUCKDB_LIBRARY=/path/to/libduckdb.dylib
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$HOME/.local"
```

`ORCHIDDB_NATIVE_LIBRARY=/path/to/library ./scripts/test.sh` also fetches the test-only DuckDB driver and runs this suite on Linux/macOS. Set `DUCKDB_ROOT` to reuse a local driver.

Normal builds do not require DuckDB or Arrow C++. The standard Arrow C ABI header is vendored with its Apache license; nlohmann_json 3.12 is required (pinned fetch fallback is provided).

Installed usage: `find_package(OrchidDB CONFIG REQUIRED)` then `target_link_libraries(app PRIVATE OrchidDB::orchiddb)`. Point `ORCHIDDB_NATIVE_LIBRARY` at the compiler shared library. For compiler request fields see [compiler documentation](https://docs.orchiddb.com/sql-compiler.html). SQL parameters are specialized literals; recompile after relevant values/schema/mappings/functions change.

## Releases

Tag `vX.Y.Z` matching CMake project version. GitHub Actions builds the exact native source in `NATIVE_REVISION`, runs integration tests, packages installable CMake headers/configuration, JSON dependency and the native library, and uploads platform `.tar.gz` assets to this repository's GitHub release. No package-manager credentials are needed for GitHub releases. No release has been published yet. License: [existing OrchidDB GPL-3.0-only license](LICENSE.md).
