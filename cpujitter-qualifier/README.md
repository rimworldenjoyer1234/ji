# cpujitter-qualifier

`cpujitter-qualifier` is an internal tooling repository for discovering and validating platform profiles consumed by `libcpujitter`.

It is intentionally separate from runtime app startup and must not be a dependency of end-user applications.

## What it does
- Detects platform details (`os`, `arch`, `cpu_vendor`)
- Loads sweep config from `configs/sweep_config.json`
- Evaluates candidate parameter combinations
- Optionally applies a small NIST integration bonus hook when external tooling is present
- Writes sweep report and exports selected profile JSON compatible with `libcpujitter`

## Layout
- `include/qualifier/` public qualifier API
- `src/` platform detection, config loading, evaluation, export, optional NIST hook
- `scripts/` helper runner script
- `configs/` sweep config examples
- `artifacts/` generated intermediate files
- `reports/` generated textual reports
- `exported_profiles/` selected profile output
- `external/jitterentropy/` placeholder
- `external/SP800-90B_EntropyAssessment/` optional NIST tooling placeholder
- `tests/` smoke test harness

## Build and run
```bash
cmake -S cpujitter-qualifier -B cpujitter-qualifier/build
cmake --build cpujitter-qualifier/build
cd cpujitter-qualifier
./build/cpujitter_qualifier
```

## Optional NIST integration
The code compiles and runs without NIST tooling.

A thin optional hook exists in `src/nist_hook.c`:
- If `enable_nist=1` and `external/SP800-90B_EntropyAssessment/run_nist.sh` exists, the hook is activated.
- TODO markers identify where real invocation/parsing should be added.

This keeps NIST integration isolated and optional.

## Export compatibility
The exported file `exported_profiles/selected_profile.json` matches the profile object format expected by `libcpujitter/profiles/index.json` entries:
- `id`
- `os`
- `arch`
- `cpu_vendor`
- `osr`
- `mem_blocks`
- `mem_block_size`
- `smoke_bytes`

## Qualification workflow
1. Detect current platform.
2. Load sweep config candidate ranges.
3. Evaluate each candidate and compute a score.
4. Optionally enrich score with NIST hook result when available.
5. Write sweep report to `reports/sweep_report.txt`.
6. Export best candidate to `exported_profiles/selected_profile.json`.
7. Copy exported JSON profile into `libcpujitter/profiles/index.json` during profile curation.
