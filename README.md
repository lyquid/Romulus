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
- **DB tab**: "Read DB" loads all tables; select a table to see a Schema panel (column type + PK/NN/UQ/FK badges) and a full read-only scrollable grid; right-click any cell to copy
- Scanned ROM directories persisted in the database — loaded automatically on startup
- Right-click any ROM Name, Size, SHA1 cell, or folder path to copy the value to clipboard (with toast notification)
- Browse buttons open native file/folder picker dialogs
- Background threading keeps the UI responsive during long operations
- Near-zero CPU usage when idle (event-driven rendering)

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

# Full pipeline (import → scan → verify) — SPEEDRUN MODE
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
| `dat_versions` | Each imported DAT file (name, version, system description, SHA-256 checksum). Unique by checksum — same file can't be imported twice. | The rulebook — what a given publisher decided correct ROMs look like. |
| `games` | Normalized game entries — one row per unique game name per DAT version. | Eliminates duplication: 10 ROMs for the same game share one `games` row. |
| `roms` | The **expected** ROM entries declared by a DAT (`expected_sha1`, `crc32`, `md5`, size). Each ROM is a child of a `games` row via `game_id` FK. | *Opinion* — what the DAT author says a correct ROM looks like. |
| `files` | Every ROM **file found on disk** (virtual path, optional archive_path, optional entry_name, size, hashes, Unix scan timestamp). Points into `global_roms` via `sha1`. | *Reality* — what is actually sitting in your scan folders. Archive entries are first-class citizens. |
| `global_roms` | **Content-addressable file identity** keyed by SHA-1. Multiple paths can map to the same content blob. | Deduplication — the same binary in two folders is one `global_rom`, two `files`. |
| `rom_matches` | Which `global_rom` satisfies each `rom`, and *how* (`match_type` integer enum). | The verdict per ROM — populated by the matcher, queried by the classifier. |
| `scanned_directories` | User-registered scan folders, persisted across sessions. | Remember where to look without re-adding every launch. |

### How They Connect

```
dat_versions ──< games ──< roms
                               │
                               └── rom_matches ──> global_roms <── files
                                    (match_type)   (SHA-1 PK)    (path on disk)
```

`rom_matches` is the **bridge**: for every `rom` (expected), it records which `global_rom` (actual) satisfies it, and *how* it was matched:

| `match_type` | Value | Meaning |
|---|---|---|
| `Exact` | 0 | CRC32 + MD5 + SHA1 all agree — gold standard |
| `Sha256Only` | 1 | Only SHA-256 matches (enriched DAT entry) |
| `Sha1Only` | 2 | Only SHA-1 matches |
| `Md5Only` | 3 | Only MD5 matches |
| `Crc32Only` | 4 | Only CRC32 matches |
| `NoMatch` | 5 | No match found |

---

## ⚙️ ~ WORKFLOW: STEP BY STEP ~

> *"All we have to decide is what to do with the ROMs that are given to us."* — Gandalf, probably

**Step 1 — IMPORT DAT** 📜

Load the sacred DAT scroll. Parses the LogiqX XML and inserts:

```bash
romulus import-dat "Nintendo - Game Boy (20240101).dat"
```

- `dat_versions` — one row for this DAT file
- `games` — one row per unique game name
- `roms` — one row per ROM entry, linked to its game

---

**Step 2 — SCAN FOLDERS** 🔍

Walk the dungeon and hash every file (CRC32 + MD5 + SHA1 in a single pass):

```bash
romulus scan /path/to/roms/GameBoy
```

- `global_roms` — content identity, keyed by SHA-1
- `files` — path → global_rom link
- Already-known files with unchanged size/mtime are skipped (smart caching!)
- Archive files (zip/7z) are opened and each entry hashed individually

---

**Step 3 — MATCH** ⚔️ *(romulus verify, step 1)*

Compares every `rom` against every `global_rom` in priority order:

```
SHA-1  →  SHA-256  →  MD5  →  CRC32
```

Inserts `rom_matches` rows with the `match_type` verdict.

---

**Step 4 — CLASSIFY** 🏷️ *(romulus verify, step 2)*

Reads `rom_matches` + `files` via CTE — no separate status table, no stale data:

| Status | Condition |
|---|---|
| ✅ **Verified** | Exact match and file exists on disk |
| ❓ **Missing** | No match entry at all |
| 🔍 **Unverified** | Partial match (SHA-1/MD5/CRC32 only) + file is live |
| ⚠️ **Mismatch** | Match was recorded but the file has since been deleted |

> 🥷 *ROM status is never stored — it is computed live from `rom_matches` + `files`, like a ninja calculating the optimal jump. It can never go stale.*

---

**Step 5 — REPORT** 📊

```bash
romulus report summary [--format text|csv|json]
romulus report missing  [--format text|csv|json]
```

---

### Verification Flow at a Glance

```
Import DAT  →  Scan  →  Hash  →  Match  →  Classify  →  Report
     │           │        │         │           │            │
     ▼           ▼        ▼         ▼           ▼            ▼
dat_versions   Files    CRC32     SHA-1      Verified      Text
games          Scan     MD5       SHA-256    Missing       CSV
roms           Skip     SHA-1     MD5        Unverified    JSON
               Arch.              CRC32      Mismatch

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
