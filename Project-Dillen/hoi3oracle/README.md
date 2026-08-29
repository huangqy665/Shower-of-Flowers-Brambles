# HOI3 Oracle

`hoi3oracle` is the preserved Windows x86 observation, reverse-probe and
in-process extension platform for HOI3 TFH 4.02. It contains the former New
Core sources, Script GUI host, native query/effect bridges, leader-capture
module, injection DLL, launcher and their probes.

It is intentionally separate from the standalone Dillen runtime under
`../src`. The two systems share a repository during the transition, but they
do not share source directories or link dependencies.

## Build only HOI3 Oracle

From `Project-Dillen/hoi3oracle`:

```powershell
cmake --preset windows-x86
cmake --build --preset windows-x86-debug
ctest --preset windows-x86-debug
```

It may also be selected from the parent transition workspace with the
`hoi3-oracle-windows-x86` presets in `../CMakePresets.json`.

The existing internal `new_core::*` target names and produced launcher/DLL
names remain stable so current injection and real-game test procedures keep
working.
