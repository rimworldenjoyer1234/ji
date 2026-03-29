# libcpujitter

`libcpujitter` is a runtime-oriented C wrapper around jitter entropy generation with a small stable API.

This initial version provides:
- Opaque context-based API (`cpujitter_ctx`)
- Runtime platform detection
- Deterministic profile selection from `profiles/index.json`
- Cache-first startup with local validated profile cache
- Short smoke test on selected profile
- Lightweight recalibration fallback with a small candidate matrix
- Byte API + fair d6 roll API (rejection sampling)
- CMake build, example app, and basic tests

## Architecture

### Public API
Public header: `include/cpujitter/cpujitter.h`

Key API functions:
- `cpujitter_init`
- `cpujitter_init_with_profile`
- `cpujitter_recalibrate`
- `cpujitter_get_bytes`
- `cpujitter_roll_die`
- `cpujitter_get_platform_info`
- `cpujitter_get_runtime_config`
- `cpujitter_get_status_json`
- `cpujitter_shutdown`
- `cpujitter_strerror`

### Internal modules
- `src/cpujitter.c`: lifecycle orchestration and API glue
- `src/platform_detect.c`: runtime platform fingerprinting
- `src/json_profiles.c`: minimal JSON profile parser + deterministic selection
- `src/cache.c`: local cache load/save abstraction (JSON)
- `src/recalibrate.c`: smoke test + lightweight recalibration matrix
- `src/entropy_backend.c`: backend adapter layer

### Backend integration model
`src/entropy_backend.c` currently uses `/dev/urandom` fallback logic so the project builds without external dependencies.

Precise future integration point:
- **TODO(jitterentropy-integration):** replace fallback backend with direct jitterentropy-library adapter in `external/jitterentropy`.

This keeps jitterentropy-specific details internal and out of public headers.

## Startup lifecycle
1. Detect current platform (`os`, `arch`, `cpu_vendor`).
2. If not forcing a specific profile, try local cache file first.
3. If cache is missing/invalid/fails smoke test, parse `profiles/index.json`.
4. Deterministically select best profile using score `(os, arch, cpu_vendor)` with stable tie-break by profile id.
5. Apply selected profile and run smoke test.
6. If smoke test fails, run lightweight recalibration candidate sweep.
7. On success, write validated profile to cache.

## Fair die rolling
`cpujitter_roll_die` uses rejection sampling:
- Draw one byte.
- Reject values `>= 252`.
- Return `(byte % 6) + 1`.

This avoids modulo bias.

## Build
```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Run example
```bash
./build/dice_app
```

## Profile format (`profiles/index.json`)
Top-level object with:
- `schema_version` (int)
- `profiles` (array)

Each profile includes:
- `id` (string)
- `os` (string)
- `arch` (string)
- `cpu_vendor` (string)
- `osr` (int)
- `mem_blocks` (int)
- `mem_block_size` (int)
- `smoke_bytes` (int)

## Cache format
Runtime writes a validated cache JSON (ignored by git by default), example template:
- `cache/local_profile.sample.json`

## Repository layout
- `include/cpujitter/` public headers
- `src/` internal implementation
- `profiles/` known platform profiles
- `examples/` minimal integration example
- `tests/` basic tests
- `external/jitterentropy/` placeholder integration directory
- `cache/` local cache location (sample tracked, generated files ignored)

## Integration in other apps
1. Include `cpujitter/cpujitter.h`.
2. Initialize with paths to profile index and cache file.
3. Call `cpujitter_get_bytes` / `cpujitter_roll_die`.
4. Shutdown with `cpujitter_shutdown`.

Keep the cache file writable by the running process.

## Related internal tooling
A separate qualification tooling repository is provided at `cpujitter-qualifier/`.
It is intended for lab/CI profile generation and is not a runtime dependency of `libcpujitter`.
