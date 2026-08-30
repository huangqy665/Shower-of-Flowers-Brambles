# Project Dillen

A mechanism-definable, load-time-compiled, deterministically-executed gameplay
runtime and standalone game-engine project for grand-strategy games.

The **authoritative** architecture, scope and roadmap document is
[`../Project Dillen工程开发备忘录.md`](../Project%20Dillen工程开发备忘录.md).
This file is only an entry point.

## What it is

- A **Kernel** that knows nothing about gameplay meaning — no built-in
  Country / War / Technology types.
- External **Packages** declare entities, components, relations, mechanisms and
  algorithms in a Clausewitz-style authoring DSL — see
  [`DILLEN_AUTHORING.md`](DILLEN_AUTHORING.md).
- A **Resolver + Runtime Compiler** freeze those declarations into a slot-based
  *Frozen Runtime Catalog*.
- A deterministic **Runtime** executes ticks, transactions, scheduled events and
  RNG streams, with a canonical save format and byte-stable replay.
- HOI3 corpus import is an **optional, currently frozen** external adapter; the
  standalone engine does not depend on it.

## Layout

| Path | Responsibility |
| --- | --- |
| `src/kernel` | IDs, schema, registries, runtime compiler, capability & transaction contracts |
| `src/world` | Authoritative world, entity/component/relation/mechanism stores, cross-store transactions |
| `src/runtime` | Scheduler, algorithm runtime, command queue, query snapshots, declarative & controlled-script VMs |
| `src/persistence` | Canonical save codec, migration registry, deterministic replay |
| `src/host` | `project-dillen` standalone CLI host and inspector |
| `src/parser` | VFS, file catalog, lexer, parser registry, authoring parsers, resolver |
| `src/adapter` | Load-time external-corpus projection identity / sealing (no gameplay semantics) |
| `demo/dillen_demo_1_0` | Pure-Dillen end-to-end demo: two external mechanism packages, swappable root ruleset |
| `tests` | `*_probe` executables registered with CTest |
| `hoi3oracle` | Independent HOI3 research / injection platform — not a standalone dependency |

Dependency direction is one-way:
`host → runtime → world → kernel → (parser / authoring contracts)`.
Gameplay packages compile *against* kernel contracts; the kernel never depends
on them.

## Build & test

Requires CMake ≥ 3.25 and Visual Studio 2022. Cross-platform (GCC/Clang,
Linux/macOS) is **not yet supported** — `cmake/DillenTargets.cmake` currently
only configures MSVC (memo §4.6).

Configure and build from this directory:

```powershell
cmake --preset dillen-standalone-windows-x64
cmake --build --preset dillen-standalone-windows-x64-debug
```

Run the tests from the repository root (probes resolve fixture paths relative to
it):

```powershell
ctest --preset dillen-standalone-windows-x64-debug --output-on-failure
```

Other presets: `dillen-compatibility-windows-x64` (adds the frozen HOI3
prototype), `hoi3-oracle-windows-x86`.

## Demo

See [`demo/dillen_demo_1_0/README.md`](demo/dillen_demo_1_0/README.md) for the
end-to-end run: two external mechanism packages, a swappable balanced /
accelerated root ruleset, save-reload and deterministic replay — with no HOI3
corpus, compatibility target or oracle.

## License

The Dillen engine (this `Project-Dillen/` tree) is released under the MIT
License — see [`LICENSE`](LICENSE). Sibling directories in the repository
(`../Project-Alice-main`, `../show of flowers`, …) are third-party material and
carry their own terms.
