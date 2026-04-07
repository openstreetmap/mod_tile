# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**mod_tile** is a high-performance tile serving system with two components:
- **mod_tile**: Apache 2 HTTP module that serves map tiles
- **renderd**: Daemon that renders tiles using Mapnik

## Build System

CMake is the primary build system (Autotools is the alternative). The project requires C99 and C++11 (C++17 for Mapnik 4+).

### Ubuntu dependencies

```sh
sudo apt --no-install-recommends --yes install \
  apache2 apache2-dev cmake curl g++ gcc git \
  libcairo2-dev libcurl4-openssl-dev libglib2.0-dev \
  libiniparser-dev libmapnik-dev libmemcached-dev librados-dev
```

### CMake Build (recommended)

```sh
export CMAKE_BUILD_PARALLEL_LEVEL=$(nproc)

cmake -B /tmp/mod_tile_build -S . \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_LOCALSTATEDIR=/var \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_INSTALL_RUNSTATEDIR=/run \
  -DCMAKE_INSTALL_SYSCONFDIR=/etc \
  -DENABLE_TESTS=ON

cmake --build /tmp/mod_tile_build

# Run all tests
cd /tmp/mod_tile_build && ctest

# Run a single test by name
cd /tmp/mod_tile_build && ctest -R <test_name> --output-on-failure

# Install
sudo cmake --install /tmp/mod_tile_build --strip
```

### Autotools Build

```sh
./autogen.sh
./configure
make
sudo make install
```

### Key CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `ENABLE_TESTS` | OFF | Build test suite (Catch2) |
| `USE_CAIRO` | ON | Cairo composite backend |
| `USE_CURL` | ON | HTTP proxy storage backend |
| `USE_MEMCACHED` | ON | Memcached storage backend |
| `USE_RADOS` | ON | Ceph RADOS storage backend |
| `MALLOC_LIB` | libc | Memory allocator: libc/jemalloc/mimalloc/tcmalloc |

### Linting / Static Analysis

CI runs `flawfinder` for security scanning (`.github/workflows/flawfinder-analysis.yml`) and a lint workflow (`.github/workflows/lint.yml`).

## Architecture

### Communication Flow

```
HTTP Client → mod_tile (Apache module)
                  ├── Storage backends (tile cache hit) → Response
                  └── Unix socket → renderd daemon
                                        └── Request queue (5 priority levels)
                                              └── Thread pool → Mapnik → Metatile storage
```

### Key Components

**`src/mod_tile.c`** — Apache module: request handling, cache expiry heuristics, delay pool rate limiting, statistics. Config structures: `tile_config_rec` (per-directory), `tile_server_conf` (server-wide).

**`src/renderd.c`** — Rendering daemon main loop; manages worker threads and Unix socket listener.

**`src/gen_tile.cpp`** — Mapnik tile generation. Called by renderd worker threads.

**`src/request_queue.c`** — Priority queue with hash-indexed deduplication. 5 levels: Normal, Priority, Low, Bulk, Dirty.

**`src/renderd_config.c`** — Parses `renderd.conf` (INI format) into `renderd_config` / `xmlconfigitem` structs.

**Storage backends** (pluggable via `includes/store.h` function-pointer interface):
- `store_file.c` — filesystem (default), stores 8×8 metatiles
- `store_memcached.c` — Memcached
- `store_rados.c` — Ceph RADOS
- `store_ro_http_proxy.c` — HTTP proxy (read-only)
- `store_ro_composite.c` — composite read-only (requires Cairo)
- `store_null.c` — no-op

### Protocol

mod_tile and renderd communicate over a Unix socket (default: `/run/renderd/renderd.sock`, TCP fallback: `localhost:7654`) using the protocol defined in `includes/protocol.h`. Protocol version is v3. Commands: `cmdRender`, `cmdDirty`, `cmdRenderPrio`, `cmdRenderLow`, `cmdRenderBulk`, `cmdDone`, `cmdNotDone`.

### Metatile Format

Tiles are stored in 8×8 metatile bundles (`METATILE = 8`) in a hashed directory structure. File format: `"META"` magic header + index of per-tile offsets/sizes. `includes/metatile.h` defines the layout; `src/metatile.cpp` is the C++ wrapper.

### Important Constants (`includes/render_config.h`, `includes/mod_tile.h`)

- `MAX_ZOOM = 20`
- `HASHIDX_SIZE = 2213` (request deduplication hash)
- Default tile directory: `/var/cache/renderd/tiles`
- `MAX_LOAD_OLD = 16`, `MAX_LOAD_MISSING = 50` — re-render thresholds

## Tests

Tests use **Catch2** (v2.13.9) and live in `tests/`. The main suites:

- `gen_tile_test.cpp` — Mapnik rendering pipeline (largest suite)
- `renderd_config_test.cpp` — configuration parsing
- `renderd_test.cpp` — daemon core
- `render_expired_test.cpp`, `render_list_test.cpp`, `render_old_test.cpp` — utility programs
- `render_speedtest_test.cpp` — performance tool

Test infrastructure uses `tests/httpd.conf.in` and `tests/renderd.conf.in` templates to spin up live Apache + renderd for integration tests. `tests/tiles.sha256sum` holds expected checksums for tile output validation.

## Repository Layout

```
includes/       — public headers (protocol, store interface, config structs)
src/            — C/C++ source for mod_tile, renderd, storage backends, utilities
tests/          — Catch2 test suites + fixtures
cmake/          — custom Find*.cmake modules
docs/build/     — per-distro build instructions
docs/man/       — man pages
etc/            — example Apache and renderd config files
utils/          — example map data
.github/        — CI workflows (build-and-test, lint, flawfinder, docker)
```
