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
`src/entropy_backend.c` integrates with jitterentropy public API when `jitterentropy.h` is available (`jent_entropy_init_ex`, collector alloc/free, `jent_read_entropy`).

If jitterentropy headers are missing, build can use an explicit **NON-PRODUCTION** mock backend (`CPUJITTER_ENABLE_MOCK_BACKEND=ON`) and emits a clear CMake warning.

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

With `CPUJITTER_BUILD_PROBE=ON` (default), build also runs `dice_app --probe` and generates:
- `build/cpujitter_probe_random.bin` (4096 random bytes)
- `build/cpujitter_probe_meta.txt` (selected runtime vars + estimated entropy/bit)

## Run example
```bash
./build/dice_app
```

## Profile format (`profiles/index.json`)
Top-level index object with:
- `schema_version` (int)
- `profiles` array of `{id, path}` entries

Each per-profile file includes:
- base fields: `id`, `os`, `arch`, `cpu_vendor`, `osr`, `smoke_bytes`
- `runtime` fields: `disable_memory_access`, `force_internal_timer`, `disable_internal_timer`, `force_fips`, `ntg1`, `cache_all`, `max_memsize`, `hashloop`
- optional `match` fields: `virtualization`, `cpu_model_exact`, `cpu_model_family`, `logical_cpu_min`, `logical_cpu_max`

## Cache format
Runtime writes a simple versioned cache JSON (ignored by git by default), example template:
- `schema_version`
- `platform_fingerprint` (`os`, `arch`, `cpu_vendor`, `cpu_model`, `virtualization`, `logical_cpu_count`)
- `validated_profile` runtime parameters

On startup, cache entries are validated against the current platform fingerprint.
Stale/incompatible/corrupt cache files are rejected cleanly and runtime falls back to bundled profiles/recalibration.
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


## Recalibration strategy
Runtime sweeps a small practical matrix: `osr=[3,4,6,8]`, `hashloop=[1,4,8]`, `max_memsize=[64,256,1024]` and timer strategies `(native, disable_internal_timer, force_internal_timer)`, preferring native + lower settings first.
