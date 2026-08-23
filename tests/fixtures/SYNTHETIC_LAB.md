# Synthetic ROM/DAT scenario lab

This is Romulus's permanent hostile collection laboratory. It contains only tiny text payloads
such as `ROMULUS_ALPHA_V1`; none of the generated files is executable or copied from commercial
software.

The C++ generator in `synthetic_fixture_lab.cpp` is shared by the integration tests and the
standalone `romulus-fixture-generator` executable. It writes the payload bytes, derives CRC32,
MD5, SHA-1, and SHA-256 through Romulus's production `HashService`, emits the contradictory DAT
XML, builds deterministic ZIP archives, and writes the expected-result manifest. Hashes are never
maintained as an independent hand-written table.

## Generated tree

```text
synthetic/
  dats/
    Synthetic Console v1.dat
    Synthetic Console v2.dat
    Other Console.dat
  sources/
    clean/
    messy_a/
    messy_b/
    duplicates/
    unknown/
    archives/
  expected/
    scenario_manifest.json
```

The manifest is the machine- and human-readable source of truth for payload hashes, stable scenario
IDs, physical paths, expected audit counts, implemented vocabulary, reserved future vocabulary, and
known architectural limits.

## Generate it manually

From a configured build directory:

```powershell
cmake --build build --config Debug --target romulus_fixture_generator
& .\build\tests\Debug\romulus-fixture-generator.exe .\build\synthetic
```

For a single-config generator, the executable is normally `build/tests/romulus-fixture-generator`.
Pass a fresh or lab-owned output directory. Known generated files are overwritten so the lab can be
regenerated, but the tool deliberately does not delete unrelated files from the destination.

## Manual GUI workflow

1. Generate `build/synthetic` with the command above.
2. Launch a disposable GUI database:
   `& .\build\apps\gui\Debug\romulus-gui.exe --db .\build\synthetic-manual.db`
   (adjust the configuration directory for your generator).
3. In **Folders**, add and scan the single root `build/synthetic/sources`. Scanning that root
   recursively includes loose files and both ZIP archives.
4. Import all three files from `build/synthetic/dats`:
   `Synthetic Console v1.dat`, `Synthetic Console v2.dat`, and `Other Console.dat`.
5. Run **Verify**, open **DATs**, select `Synthetic Console` version 1, and compare the cards/rows
   with the v1 table below.
6. Select version 2 without rescanning. Compare it with the v2 table below.
7. Inspect `build/synthetic/expected/scenario_manifest.json` for exact hashes, physical paths, and
   the full symbolic scenario vocabulary.

### Expected version 1 audit (after all DATs are imported)

| Scenario ID / file | Expected state | Why |
|---|---|---|
| `alpha` | Correct | Exact bytes resolve to the shortest canonical loose path. |
| `beta` | Wrong canonical name | Exact v1 bytes are stored as `Beta Goblin.rom`. |
| `gamma_missing` | Missing | The DAT expects gamma; no gamma bytes are materialized. |
| `archive_exact` | Correct | The ZIP entry name is canonical. |
| `archive_wrong_name` | Wrong canonical name | Exact bytes live in `Archive Goblin.rom`. |
| `md5_fallback` | MD5 match | The DAT intentionally declares MD5 only. |
| `crc32_fallback` | CRC match | The DAT intentionally declares CRC32 only. |
| `hash_metadata_conflict` | MD5/strong-hash partial match | SHA-1 identifies the probe, while MD5 deliberately belongs to alpha. |
| `Delta (World).rom` | Extra / known elsewhere | It is introduced by v2, not expected by v1. |
| `Beta (World).rom` | Extra / known elsewhere | It carries the changed v2 beta bytes. |
| `Other Console Exclusive.rom` | Extra / known elsewhere | It is recognized only by Other Console. |
| `Mystery Goblin.rom` | Globally unknown | No imported DAT declares its content. |
| four alpha locations | Duplicate physical copies | Three loose copies plus one archive entry share alpha bytes. |

Summary: 8 expected, 2 correct, 2 wrong-name, 1 missing, 1 CRC match, 2 MD5/strong-hash
partial matches, 4 extras (3 known elsewhere + 1 globally unknown), and 4 duplicate rows.

### Expected version 2 audit

| Scenario ID | Expected state | Version contradiction |
|---|---|---|
| `alpha` | Wrong canonical name | Same bytes as v1, renamed from `World` to `USA`. |
| `beta` | Correct | Same canonical name as v1, but v2 expects different bytes. |
| `delta_added` | Correct | New expectation in v2. |
| `archive_exact` | Correct | Unchanged archive expectation. |
| `archive_wrong_name` | Wrong canonical name | Unchanged bytes and still deliberately misnamed. |
| `gamma_missing` | No expectation row | v2 removed gamma. |

Summary: 5 expected, 3 correct, 2 wrong-name, 0 missing, 6 extras (5 known elsewhere +
1 globally unknown), and the same 4 alpha duplicate rows.

## Deliberate boundaries

- The generator emits SHA-256 for full DAT entries and records it in the manifest. The current DAT
  parser/import path does not persist the `sha256` attribute, so SHA-256-only DAT matching is not a
  real-pipeline scenario yet.
- `Matcher::match_all` records one winning content identity per DAT ROM. A true multi-identity
  `HashConflict` status therefore remains a lower-level database/`DatAuditor` unit fixture.
- The lab does not brute-force a CRC32 collision. Existing matcher/database fixtures are a clearer,
  deterministic way to cover that policy.
- Reserved manifest IDs name future #129/#130/#132 situations only. No operation planning,
  renaming, Set Builder, or hardlink behavior is implemented here.

Extend this generator and vocabulary for later roadmap work instead of creating disconnected
one-off fixture families.
