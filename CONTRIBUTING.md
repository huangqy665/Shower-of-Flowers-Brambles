# Contributing

## Architectural authority

`Project Dillen工程开发备忘录.md` is the single source of truth for architecture,
system boundaries, dependency direction and roadmap. If a change alters kernel
boundaries, authoritative-state ownership, dependency direction, ruleset
semantics or the external-compatibility architecture, update that document **in
the same change**.

`Project-Dillen/FROZEN_CONTRACTS.md` lists the contract surface frozen by
Demo 0.2 and, for each item, the guard that enforces it. Read it before
touching the save codec, the serialized `std::variant` types, the stable-ID
hashes or the Capability invocation ABI. A guard that fails is asking you one
question: *did you mean to change the format?* If not, fix the code — never
re-baseline a golden value to make a build go green.

## Commit messages

One `<area>: <imperative summary>` line (≤ 72 chars), then an optional body that
explains *why*.

```
kernel: freeze WorldTransactionResult status enum
runtime: run destroy stage after tick dispatch
persistence: reject save images whose ruleset fingerprint differs
docs: record scheduler phase contract
```

`area` ∈ `kernel`, `world`, `runtime`, `persistence`, `host`, `parser`,
`authoring`, `adapter`, `compat`, `oracle`, `demo`, `tests`, `build`, `ci`,
`docs`.

Avoid opaque numeric messages. Keep unrelated changes in separate commits.

## Before you push

```powershell
cmake --build --preset dillen-standalone-windows-x64-debug
ctest  --preset dillen-standalone-windows-x64-debug --output-on-failure
```

CI runs the same on `windows-2022`.

## Do not commit

- Build directories (`build*/`, `out/`) — covered by `.gitignore`.
- IDE state (`.vs/`, `.idea/`, per-user `.vscode/*`).
- Local run artifacts (`*.log`, `reverse_probe_runtime.*`, …).
- Large third-party corpora. Reference engines and game-content mods do not
  belong in this repository's history — keep them out of tree (git submodule,
  sibling checkout, or a pinned release archive).

## Style

- C++17, 4-space indent, Allman braces, ~80-column soft limit (see
  `.editorconfig`).
- Strong-typed IDs; result-type returns over exceptions in
  kernel / world / runtime.
- No gameplay semantics in the kernel — a new ordinary mechanism is package
  content, not kernel C++.
