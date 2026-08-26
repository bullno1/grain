# AGENTS.md

This file provides guidance to coding agents when working with code in this repository.

## Project overview

Grain is a GPU-driven composable particle system with its own DSL, built on [Cute Framework](https://github.com/RandyGaul/cute_framework). Written in C (C23, `CMAKE_C_EXTENSIONS OFF`).

## Commands

First-time setup: `./bootstrap` (git-lfs pull + submodule init; all deps are submodules in `deps/`).

All build scripts accept `BUILD_TYPE` (default `RelWithDebInfo`) and `RELOADABLE` (default `ON`) as environment variables. Binaries land in `bin/<platform>/<BUILD_TYPE>-<reloadable|static>/`.

- Build (Linux): `cmd/linux/build`
- Run editor: `cmd/linux/run` (launches `grain-editor` under gdb)
- Rebuild on file change: `cmd/linux/watch` (inotify loop over `editor`, `deps`, `lib`, `tests`)
- Tests: `cmd/linux/build && cmd/linux/test`
- Single test: pass exact suite name, optionally exact test name, to the test binary:
  `bin/linux/RelWithDebInfo-reloadable/grain-tests <suite> [test]`
  (e.g. `grain-tests decorator/scanner`)
- Web: `cmd/web/build` (emscripten; forces `RELOADABLE=OFF`, tests are excluded). Windows: `cmd/win/prepare.bat` then `cmd/win/build.bat`. Steam Runtime: `cmd/steamrt/*`.

CMake exports `compile_commands.json` into `.build/<platform>/<config>/`.

## Architecture

Three targets plus data:

- `lib/` — the `grain` static library (the actual particle system). Public API: `lib/include/grain.h`.
- `editor/` — `grain-editor`, an interactive authoring app built on the bgame framework (dear imgui UI, scene system).
- `tests/` — `grain-tests`, headless unit tests using btest from `deps/blibs`.
- `modules/` — example DSL modules, organized by kind: `emitters/`, `affectors/`, `renderer/`.

### The DSL and module model

A module is a `.glsl` file with a kind declaration (`Emitter(Name)`, `Affector(Name)`, or `Renderer(Name)`), `Requires(...)` (per-particle attributes), `Params(...)` (per-module tunables, optionally annotated with `@decorator(...)` such as `@range(min = 0)`), and a GLSL `void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx)` entry point. Kinds: **emitter** (initializes particles), **affector** (updates each particle), **renderer** (vert + frag stages; exactly one per archetype). The declared kind is enforced by `grain_define_emitter/affector/renderer`; `grain_define_module` accepts any kind and returns a tagged `grain_module_ref_t` for callers that dispatch on it. An **archetype** is a combination of modules; grain composes the union of all `Requires` blocks into a single `ParticleAttrs` struct, pools systems per archetype, and batches update/render into single draw calls. See `README.md` for full semantics, including live-reload rules (param migration vs. structural reset).

### Library internals (`lib/src/`)

- `grain.c` — runtime: module/archetype registries, pools, systems, GPU resources, update/render scheduling. Central state is `struct grain_s` in `internal.h`.
- `dsl.c` — parses module source, composes archetype shaders (using templates in `lib/src/glsl/`), and compiles GLSL→SPIR-V on the CPU via the bundled `cute_spirv.h`. This is why module definition works without a GPU.
- `decorator.c` — scanner/parser for `@decorator` annotations; decorators are stripped from the source before GLSL compilation and surfaced through `grain_param_info_t` (used by the editor for UI hints).
- `clock.h` — the per-system emission clock (pure CPU, header-only). Emission is **counted, not timed**: the CPU integrates an emission counter (`emitted += rate * dt`; `grain_burst` queues a separate burst count) and each update pass hands the GPU counter windows in a `grain_clock_entry_t` (kept exactly two vec4s wide for the GLES texel path). `grain_schedule()` in `lib/src/glsl/internal.glsl` maps window positions to slots: the k-th particle ever emitted lands in slot `k mod pool_size`, and recycling is pure ring wraparound — no free list, no kill. Safety rests on capacity: `pool_size = ceil(max_emission_rate * lifetime_budget) + max_burst_size`.
- `blueprint.c` — JSON blueprint save/load: a self-contained snapshot of a particle system (embedded module sources, archetype composition, pool config, current param values). **Any new field in `grain_pool_opts_t` or other saved system state must round-trip through it**: parse + emit + capture + `grain_blueprint_pool_opts` in `blueprint.c`, the blueprint-load sync in `editor/scenes/editor.c`, and a round-trip assertion in `tests/blueprint.c`. New JSON fields should be optional on load with a default, so older blueprints keep parsing.
- `lib/src/glsl/update.vert.glsl` is precompiled at build time into `lib/src/gen/update_vert_bytecode.h` via the `compile_vertex_shader` CMake helper.

### Editor (`editor/`)

bgame app with scenes in `editor/scenes/` (`editor.c` is the main authoring UI, `particle.c` a demo scene). Uses `SCENE_VAR`/`BGAME_VAR` for state that survives live C-code reload (the `RELOADABLE=ON` build uses `deps/remodule` for this). Module files loaded in the editor are watched with `bresmon` for hot reload (not on emscripten — guard `#ifndef __EMSCRIPTEN__`).

### Tests (`tests/`)

Tests include `lib/src` directly and use `tests/shared.h`, which constructs a `grain_t` with only an arena and no GPU resources: everything up to `grain_define_*` (DSL parsing, decorators, module definition, error reporting) is testable headlessly; archetypes and pools need a GPU and are out of scope. Two more things are verifiable without a GPU: the emission clock is pure CPU (`tests/clock.c` includes `clock.h` directly, no `test_grain` fixture needed), and edits to `lib/src/glsl/api.glsl` and `lib/src/glsl/internal.glsl` are compile-checked by the existing module-definition tests, because the inspect stub `#include`s both into the CPU-side GLSL→SPIR-V compile (`api.glsl` before the module, `internal.glsl` after it, so internal builtins stay invisible to user code). Each test file defines a `btest_suite_t` with `init_per_test`/`cleanup_per_test`; use `GRAIN_EXPECT_ERROR_CONTAINS` for error-path assertions. New test files must be added to `tests/CMakeLists.txt`.
