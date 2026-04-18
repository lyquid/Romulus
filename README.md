# 🕹️ ROMULUS

```
██████╗  ██████╗ ███╗   ███╗██╗   ██╗██╗     ██╗   ██╗███████╗
██╔══██╗██╔═══██╗████╗ ████║██║   ██║██║     ██║   ██║██╔════╝
██████╔╝██║   ██║██╔████╔██║██║   ██║██║     ██║   ██║███████╗
██╔══██╗██║   ██║██║╚██╔╝██║██║   ██║██║     ██║   ██║╚════██║
██║  ██║╚██████╔╝██║ ╚═╝ ██║╚██████╔╝███████╗╚██████╔╝███████║
╚═╝  ╚═╝ ╚═════╝ ╚═╝     ╚═╝ ╚═════╝ ╚══════╝ ╚═════╝ ╚══════╝
```

> *Imposes order on chaos — and keeps a record of it.* 👾

**ROMULUS** is a **C++23** backend system for verifying and cataloging video game ROM collections using [No-Intro](https://www.no-intro.org/) DAT files.

Because your collection deserves better than a folder named `roms_FINAL_v2_USE_THIS`.

---

## 🎮 ~ INSERT COIN TO CONTINUE ~

Whether you're a seasoned cartridge archaeologist 🏺 or a fresh-faced emulator enthusiast, ROMULUS is your trusty guide through the sprawling dungeon of your ROM library.

ROMULUS takes your DAT files and ROM directories, then tells you exactly what you have, what you're missing, and what doesn't match — with all the precision of a speedrunner and none of the motion blur.

```bash
romulus import-dat "Nintendo - Game Boy (20240101).dat"
romulus scan /path/to/roms/GameBoy
romulus verify
romulus report summary
```

> ```
> ROMULUS — Collection Summary 🏆
> ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
> System:     Nintendo - Game Boy
> Total ROMs: 1437
> Verified:   1285 (89%)  ✅
> Missing:    134         ❓
> Unverified: 12          🔍
> Mismatch:   6           ⚠️
> ```

---

## ⭐ ~ POWER-UPS ~

> *Collect them all!*

- 🗂️ **DAT Import** — Parses No-Intro LogiqX XML format, the sacred scrolls of preservation
- 🎮 **Normalized Games** — Each game name stored once per DAT version; no duplicates, ready for metadata
- 🗜️ **Archive Support** — Reads zip/7z files without extracting to disk — no mess, no fuss
- ⚡ **Parallel Hashing** — CRC32 + MD5 + SHA1 in a single pass using all CPU cores — *TURBO MODE*
- 🧠 **Smart Scanning** — Skips unchanged files, tracks modifications — smarter than a save-state
- 🎯 **Multi-Hash Matching** — SHA1 › SHA256 › MD5 › CRC32 priority — triple-verified, like a 100% save file
- 📊 **Reports** — Summary, missing ROMs, duplicates in text/CSV/JSON — the high score board
- 🗂️ **Multi-DAT** — Import and track multiple DAT files in one database — all your cartridges, one shelf

---

## 🗺️ ~ LEVEL SELECT ~

```
lib/romulus/          →  🧩  Core C++ library (all business logic)
apps/cli/             →  🖥️  CLI frontend (builds the `romulus` command)
apps/gui/             →  🎮  ImGui + GLFW desktop GUI (optional, toggleable)
apps/api/  (future)   →  🌐  REST API server for web frontend
web/       (future)   →  ⚛️  React/TypeScript web interface
```

---

## 🚀 ~ PRESS START ~

### 🎒 Requirements — Your Loadout

Before you can save the princess, you'll need:

- 🔧 **C++23 compiler** — MSVC 17.8+, GCC 13+, or Clang 17+
- 📦 **CMake** ≥ 3.25
- 🧰 **vcpkg** — the item shop of package managers

> ⚠️ **Copilot Agent note**: `vcpkg` downloads can fail in Copilot agent environments when firewall/network policy blocks package endpoints.
> By default, treat CI as the source of truth for full `vcpkg` dependency resolution.
> If needed, repository admins can enable the required outbound access for Copilot agent firewall/proxy settings and use `.github/workflows/copilot-setup-steps.yml` to preinstall dependencies, align the GCC toolchain with CI, and seed a workspace-local vcpkg cache for faster repeated runs.

### 🔨 Build — Stage 1: Dev Mode

```bash
# ↑ ↑ ↓ ↓ ← → ← → B A  — Configure (Debug)
cmake --preset dev

# BUILD
cmake --build --preset dev

# Run tests — Don't skip these or the final boss wins
ctest --preset dev

# Dev builds copy repository DAT artifacts beside the CLI binary
# so `romulus import-dat` can use the bundled DAT with no path.
```

### 🏁 Release Build — Final Stage

```bash
cmake --preset release
cmake --build build --config Release
```

> 💡 *Pro tip: Release builds are optimized for performance and size — like cartridges with the save battery still intact. They just work.*

### 🎮 GUI Build — Optional Boss Stage

The ImGui + GLFW desktop GUI is built by default. To disable it:

```bash
cmake --preset dev -DROMULUS_ENABLE_GUI=OFF
```

**System dependencies** (Ubuntu/Debian):

```bash
sudo apt install libimgui-dev libglfw3-dev libgl-dev libstb-dev
```

Or via **vcpkg** (automatic with the manifest). Note that even with vcpkg, `glfw3` requires the following X11/OpenGL system headers — vcpkg does **not** provide these:

```bash
sudo apt install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl-dev
```

```bash
# Launch the GUI
./build/apps/gui/romulus-gui

# Launch with a specific database
./build/apps/gui/romulus-gui --db /path/to/romulus.db

# Skip the GUI (headless mode)
./build/apps/gui/romulus-gui --no-gui
```

**GUI features:**

- Four-tab layout: **DATs** (ROM checklist + DAT controls), **Folders** (scan directory management), **DB** (read-only database explorer), **Log** (application log)
- DAT import, folder scanning, verification, and database purge
- ROM checklist table with Status, ROM Name, Size, and SHA1 columns
- Filter bar: free-text name filter + status dropdown (All / Verified / Missing / Unverified / Mismatch)
- Status breakdown summary: color-coded counts with completion percentage
- Active DAT shown in a full-width highlighted banner (name, version, import date)
- **DB tab**: "Read DB" loads all tables; select a table to see a Schema panel (column type + PK/NN/UQ/FK badges) and a full read-only sortable grid with a free-text filter bar and ^ / v navigation arrows; right-click any cell to copy
- **Folders tab**: registered scan directories are listed with their scanned file count (including archive entries); supports adding, removing, and rescanning folders
- Right-click any ROM Name, Size, SHA1 cell, or folder path to copy the value to clipboard (with toast notification)
- Browse buttons open native file/folder picker dialogs
- Background threading keeps the UI responsive during long operations
- Near-zero CPU usage when idle (event-driven rendering)
- GUI implementation is split by tab source files with a centralized `GuiState` model for maintainability

---

## 🕹️ ~ CONTROLS ~

```bash
# Import a DAT file — Accepting the quest
romulus import-dat
romulus import-dat path/to/dat_file.dat

# Scan a ROM directory — Scouting the dungeon
romulus scan /path/to/roms

# Match files and classify ROM status — The boss fight
romulus verify

# Full pipeline (scan → import → verify) — SPEEDRUN MODE
romulus sync path/to/dat.dat /path/to/roms

# Reports — Check your high scores
romulus report summary                   # Text summary
romulus report missing --format json     # Missing ROMs as JSON
romulus report summary --format csv      # CSV export
romulus report summary --dat "Nintendo - Game Boy"

# List imported DAT versions — Your rulebook
romulus dats

# Quick status check — How's the party doing?
romulus status
```

---

## 🗄️ ~ HOW THE APP & DATABASE WORK ~

The core purpose of Romulus is simple: **compare ROM files on your hard drive against what a DAT file says should exist**.
The schema is built around that single question.

### The Tables and Their Roles

| Table | What it stores | Why it exists |
|---|---|---|
| `dat_versions` | Each imported DAT file (name, version, system description, `dat_sha256`). Unique by `dat_sha256` — same file can't be imported twice. | The rulebook — what a given publisher decided correct ROMs look like. |
| `games` | Normalized game entries — one row per unique game name per DAT version. | Eliminates duplication: 10 ROMs for the same game share one `games` row. |
| `roms` | The **expected** ROM entries declared by a DAT (`expected_sha1`, `crc32`, `md5`, size). Each ROM is a child of a `games` row via `game_id` FK. | *Opinion* — what the DAT author says a correct ROM looks like. |
| `files` | Every ROM **file found on disk** (virtual path, optional archive_path, optional entry_name, size, hashes, Unix scan timestamp). Points into `global_roms` via `sha1`. | *Reality* — what is actually sitting in your scan folders. Archive entries are first-class citizens. |
| `global_roms` | **Content-addressable file identity** keyed by SHA-1. Multiple paths can map to the same content blob. | Deduplication — the same binary in two folders is one `global_rom`, two `files`. |
| `rom_matches` | Which `global_rom` satisfies each `rom`, and *how* (`match_type` integer enum). | The verdict per ROM — populated by the matcher, queried by the classifier. |
| `rom_status_cache` | **Precomputed per-ROM status** (0=Verified / 1=Missing / 2=Unverified / 3=Mismatch). Refreshed after every `verify` run. | Replaces the expensive CTE+JOIN on every summary or checklist query — SQLite's answer to a materialized view. |
| `scanned_directories` | User-registered scan folders, persisted across sessions. | Remember where to look without re-adding every launch. |

### How They Connect

```
dat_versions ──< games ──< roms
                               │
                               └── rom_matches ──> global_roms <── files
                                    (match_type)   (SHA-1 PK)    (path on disk)
                                         │
                                         └──> rom_status_cache
                                              (precomputed status)
```

`rom_matches` is the **bridge**: for every `rom` (expected), it records which `global_rom` (actual) satisfies it, and *how* it was matched:

| `match_type` | Value | Meaning |
|---|---|---|
| `Exact` | 0 | All available hashes agree (SHA-1 / SHA-256 / MD5 / CRC32 — each check skipped when either side lacks the hash) |
| `Sha256Only` | 1 | SHA-256 matched; other hashes disagreed or were absent |
| `Sha1Only` | 2 | SHA-1 matched; other hashes disagreed or were absent |
| `Md5Only` | 3 | MD5 matched |
| `Crc32Only` | 4 | CRC32 matched (weakest) |
| `NoMatch` | 5 | No match found |

---

## 🎯 ~ MATCH PRIORITY POLICY ~

> *"Know thy hashes, and let none confound thee." — Gandalf, again*

### Tier Order (highest to lowest)

Every DAT ROM is matched against the scanned `global_roms` table using the following priority:

| Priority | Hash | Notes |
|---|---|---|
| 1 | **SHA-1** | Industry standard for No-Intro / Redump DATs. Fastest reliable match. |
| 2 | **SHA-256** | Used when the DAT provides SHA-256 but no SHA-1 (enriched DATs). |
| 3 | **MD5** | Weaker fallback for legacy or partial DAT entries. |
| 4 | **CRC32** | Weakest — collisions are rare but possible. Multiple candidates use the tiebreaker below. |

The first tier that finds a matching `global_rom` wins and no lower tier is attempted.

### Exact vs Partial Classification

A match is classified as **`Exact`** when *every* hash declared by the DAT ROM agrees with the matched `global_rom` (hashes absent from either side are skipped):

- SHA-1 **and** SHA-256 **and** MD5 **and** CRC32 all agree → `Exact`
- Only SHA-1 agreed (SHA-256 or lower hash disagreed) → `Sha1Only`
- SHA-256 led the match but a lower hash disagreed → `Sha256Only`

This means enriched DATs that carry SHA-256 get full cross-validation — a SHA-1 match with a mismatched SHA-256 is flagged as `Sha1Only`, not `Exact`.

### Within-Tier Tiebreaker (CRC32 only)

CRC32 is the only tier where multiple `global_roms` can share the same hash value (collisions). When that happens, the **best candidate** is selected using this ordered rule chain:

| Rank | Rule | Rationale |
|---|---|---|
| 1 | **Bare (non-archive) file** preferred over archive entry | Bare files are unambiguous — no zip/7z extraction needed |
| 2 | **Shortest virtual path** among candidates at same rank | Shorter paths are typically closer to the root scan folder |
| 3 | **Latest `last_write_time`** if paths are same length | Most recently modified file is the most "current" copy |
| 4 | **Lexicographically smallest SHA-1** | Deterministic fallback — always produces the same answer |

Phantom candidates (no file currently on disk) always lose to candidates with an existing file.

> 🔒 *Policy source of truth: [`lib/romulus/engine/matcher.cpp`](lib/romulus/engine/matcher.cpp) — `pick_best_crc32_candidate()`. This table mirrors that function exactly.*

---

## ⚙️ ~ WORKFLOW: STEP BY STEP ~

> *"All we have to decide is what to do with the ROMs that are given to us."* — Gandalf, probably

**Step 1 — SCAN FOLDERS** 🔍

Walk the dungeon and hash every file (CRC32 + MD5 + SHA1 + SHA-256 in a single pass):

```bash
romulus scan /path/to/roms/GameBoy
```

- `global_roms` — content identity, keyed by SHA-1
- `files` — path → global_rom link
- Already-known files with unchanged size/mtime are skipped (smart caching!)
- Archive files (zip/7z) are opened and each entry hashed individually
- Scanning is **independent of DATs** — scan once, verify against many DATs

---

**Step 2 — IMPORT DAT** 📜

Load the sacred DAT scroll. Parses the LogiqX XML and inserts:

```bash
romulus import-dat "Nintendo - Game Boy (20240101).dat"
```

- `dat_versions` — one row for this DAT file
- `games` — one row per unique game name
- `roms` — one row per ROM entry, linked to its game

---

**Step 3 — MATCH** ⚔️ *(romulus verify, step 1)*

Compares every `rom` against every `global_rom` in priority order:

```
SHA-1  →  SHA-256  →  MD5  →  CRC32
```

Each tier cross-validates all available hashes to determine `Exact` vs a partial match type.
When CRC32 yields multiple candidates, the tiebreaker selects the best one (see *Match Priority Policy* above).

Inserts `rom_matches` rows with the `match_type` verdict.

---

**Step 4 — CLASSIFY** 🏷️ *(romulus verify, step 2)*

Reads `rom_matches` + `files` and assigns a status to each ROM:

| Status | Condition |
|---|---|
| ✅ **Verified** | Exact match and file exists on disk |
| ❓ **Missing** | No match entry at all |
| 🔍 **Unverified** | Partial match (SHA-1/MD5/CRC32 only) + file is live |
| ⚠️ **Mismatch** | Match was recorded but the file has since been deleted |

After classification, the computed statuses are written to `rom_status_cache` — a persistent
precomputed table that acts as a **materialized view**. Subsequent summary and ROM-checklist
queries read from this fast cache instead of re-running the expensive CTE+JOIN every time.

> 💾 *First query after `verify` is free — the cache pays for itself immediately on the next report or GUI reload.*

---

**Step 5 — REPORT** 📊

```bash
romulus report summary [--format text|csv|json]
romulus report missing  [--format text|csv|json]
```

---

### Verification Flow at a Glance

```
Scan/Hash  →  Import DAT  →  Match  →  Classify  →  Cache  →  Report
    │              │            │           │           │          │
    ▼              ▼            ▼           ▼           ▼          ▼
Files         dat_versions   SHA-1      Verified   rom_status  Text
Scan          games          MD5        Missing    _cache      CSV
Skip          roms           CRC32      Unverified (fast read) JSON
Arch.                        SHA-256    Mismatch

👾  "It's dangerous to go alone! Take this pipeline."  👾
```

---

## 🛠️ ~ THE PARTY MEMBERS ~

| Component | Technology | Role |
|---|---|---|
| 💻 Language | C++23 | The hero of our story |
| 🏗️ Build | CMake 3.25+ / vcpkg | Dungeon architect |
| 🗄️ Database | SQLite3 (WAL mode) | The wizard's tome |
| 📄 XML Parsing | pugixml | Scroll reader |
| #️⃣ Hashing | OpenSSL (MD5/SHA1) + constexpr CRC32 | The rogue |
| 📦 Archives | libarchive (zip/7z/tar) | Treasure chest handler |
| ⌨️ CLI | CLI11 | The bard (always talking) |
| 🎮 GUI | ImGui + GLFW + OpenGL3 | The enchanted mirror |
| 📝 Logging | spdlog | The chronicler |
| 🔗 JSON | nlohmann-json | The translator |
| 🧪 Testing | Google Test | Quality assurance paladin |

---

## 🤝 ~ JOIN THE GUILD ~

Want to help shape this legendary artifact? Here's the Guild Code:

- 📜 Follow the C++ Core Guidelines (see `.github/copilot-instructions.md`)
- 💬 Conventional Commits: `feat(scope)`, `fix(scope)`, `refactor(scope)`
- ✨ Format code with `clang-format` before committing — untidy code is like a corrupted save file
- 🤖 All PRs must pass CI (MSVC + GCC + Clang) — no softlocking the build pipeline

> 🎲 *"With great power comes great `git blame`."*

---

## 🔐 ~ SECRET KONAMI CHEAT CODE ~

```
↑ ↑ ↓ ↓ ← → ← → B A
```

*Doesn't actually do anything. But you tried it anyway, didn't you? Respect.* 🫡
