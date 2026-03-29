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
2. If not forcing a specific profile, try local validated cache profile first.
3. If cache profile exists, apply it and run smoke test.
4. If cache path fails, parse `profiles/index.json`.
5. Deterministically select bundled profile (or requested profile) and run smoke test.
6. If bundled profile path fails, run lightweight recalibration over a small candidate matrix (no heavy qualification).
7. On success, persist local validated cache profile and keep reusable backend/context initialized.
8. Expose selected runtime config and status JSON via public API.

### Debug logging hook
Set `CPUJITTER_LOG=1` to emit lifecycle logs to `stderr` during init/recalibration decisions.


## Deterministic profile matching
`profiles/index.json` now references per-profile JSON files.
Matching input uses platform OS, arch, cpu vendor, cpu model, virtualization type, and logical CPU count.
Selection rules:
- exact core match required (`os`, `arch`, `cpu_vendor`)
- CPU model exact rule beats family rule
- family rule supports simple `*` suffix pattern (e.g. `x86-*`)
- bare-metal profiles are preferred unless platform virtualization is `vm`
- tie-breaks are stable by profile `id` lexical order

`cpujitter_get_status_json` includes a match explanation string for debugging selection outcomes.

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
Top-level index object with:
- `schema_version` (int)
- `profiles` array of `{id, path}` entries

Each per-profile file includes:
- base fields: `id`, `os`, `arch`, `cpu_vendor`, `osr`, `mem_blocks`, `mem_block_size`, `smoke_bytes`
- optional `match` fields: `virtualization`, `cpu_model_exact`, `cpu_model_family`, `logical_cpu_min`, `logical_cpu_max`

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
