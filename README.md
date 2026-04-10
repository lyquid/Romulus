# 🕹️ ROMULUS

```text
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

## 🎮 INSERT COIN TO CONTINUE

Whether you're a seasoned cartridge archaeologist 🏺 or a fresh-faced emulator enthusiast, ROMULUS is your trusty guide through the sprawling dungeon of your ROM library.

ROMULUS takes your DAT files and ROM directories, then tells you exactly what you have, what you're missing, and what doesn't match — with all the precision of a speedrunner and none of the motion blur.

```bash
romulus import-dat "Nintendo - Game Boy (20240101).dat"
romulus scan /path/to/roms/GameBoy
romulus verify
romulus report summary
```

```text
╔══════════════════════════════════════════════════╗
║       ROMULUS — Collection Summary  🏆           ║
╠══════════════════════════════════════════════════╣
║ System:     Nintendo - Game Boy                  ║
╠══════════════════════════════════════════════════╣
║ Total ROMs: 1437                                 ║
║ Verified:   1285 (89%)  ✅                       ║
║ Missing:    134         ❓                       ║
║ Unverified: 12          🔍                       ║
║ Mismatch:   6           ⚠️                       ║
╚══════════════════════════════════════════════════╝
```

---

## ⭐ Power-Ups (Features)

> *Collect them all!*

| Power-Up | Effect |
| ---------- | -------- |
| 🗂️ **DAT Import** | Parses No-Intro LogiqX XML format — the sacred scrolls of preservation |
| 🗜️ **Archive Support** | Reads zip/7z files without extracting to disk — no mess, no fuss |
| ⚡ **Parallel Hashing** | CRC32 + MD5 + SHA1 in a single pass using all CPU cores — *TURBO MODE* |
| 🧠 **Smart Scanning** | Skips unchanged files, tracks modifications — smarter than a save-state |
| 🎯 **Multi-Hash Matching** | SHA1 > MD5 > CRC32 priority — triple-verified, like a 100% save file |
| 📊 **Reports** | Summary, missing ROMs, duplicates in text/CSV/JSON — the high score board |
| 🌍 **Multi-System** | Track multiple systems in one database — all your cartridges, one shelf |

---

## 🗺️ Level Select (Architecture)

```text
lib/romulus/          → 🧩 Core C++ library (all business logic)
apps/cli/             → 🖥️  CLI frontend (builds the `romulus` command)
apps/gui/             → 🎮 ImGui + GLFW desktop GUI (optional, toggleable)
apps/api/  (future)   → 🌐 REST API server for web frontend
web/       (future)   → ⚛️  React/TypeScript web interface
```

---

## 🚀 Press START — Getting Started

### 🎒 Requirements (Your Loadout)

Before you can save the princess, you'll need:

- 🔧 **C++23 compiler**: MSVC 17.8+, GCC 13+, or Clang 17+
- 📦 **CMake** ≥ 3.25
- 🧰 **vcpkg** — the item shop of package managers

### 🔨 Build (Stage 1 — Dev Mode)

```bash
# ↑ ↑ ↓ ↓ ← → ← → B A  — Configure (Debug)
cmake --preset dev

# ▶️  BUILD
cmake --build --preset dev

# 🧪 Run tests — Don't skip these or the final boss wins
ctest --preset dev

# 📦 Dev builds copy repository DAT artifacts beside the CLI binary
# so `romulus import-dat` can use the bundled DAT with no path.
```

### 🏁 Release Build (Final Stage)

```bash
cmake --preset release
cmake --build build --config Release
```

> 💡 *Pro tip: Release builds are optimized for performance and size — production-ready binaries. Like cartridges with the save battery still intact, they just work.*

### 🎮 GUI Build (Optional Boss Stage)

The ImGui + GLFW desktop GUI is built by default. To disable it:

```bash
cmake --preset dev -DROMULUS_ENABLE_GUI=OFF
```

**System dependencies** (Ubuntu/Debian):
```bash
sudo apt install libimgui-dev libglfw3-dev libgl-dev libstb-dev
```

Or via **vcpkg** (automatic with the manifest). Note that even with vcpkg, `glfw3` requires the following X11/OpenGL system headers to be installed — vcpkg does **not** provide these:

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
- Status breakdown summary: color-coded verified/missing/unverified/mismatch counts with completion percentage
- Active DAT shown in a full-width highlighted banner (name, version, import date)
- **DB tab**: database path in a disabled text field + Browse button (future use); "Read DB" loads all tables; select a table to see a Schema panel (column type + PK/NN/UQ/FK badges) and a full-rows read-only scrollable grid; right-click any cell to copy
- Scanned ROM directories persisted in the database — loaded automatically on startup
- Right-click any ROM Name, Size, SHA1 cell, or folder path to copy the value to clipboard (with toast notification)
- Browse buttons open native file/folder picker dialogs
- Background threading keeps the UI responsive during long operations
- Near-zero CPU usage when idle (event-driven rendering)

---

## 🕹️ Controls (Usage)

```bash
# 📥 Import a DAT file — Accepting the quest
romulus import-dat
romulus import-dat path/to/dat_file.dat

# 🔍 Scan a ROM directory — Scouting the dungeon
romulus scan /path/to/roms

# ✅ Match files and classify ROM status — The boss fight
romulus verify

# ⚡ Full pipeline (import → scan → verify) — SPEEDRUN MODE
romulus sync path/to/dat.dat /path/to/roms

# 📊 Reports — Check your high scores
romulus report summary                    # 📝 Text summary
romulus report missing --format json      # ❓ Missing ROMs as JSON
romulus report summary --format csv       # 📋 CSV export
romulus report summary --system "Nintendo - Game Boy"

# 🌍 List known systems — Your game library
romulus systems

# ⚡ Quick status check — How's the party doing?
romulus status
```

---

## ⚙️ Pipeline (The Journey)

Each stage processes ROMs sequentially — the output of one feeds the next, with verification results classified into one of the status categories shown.

```text
DAT Import → Scan → Hash → Match → Classify → Report
    │          │       │       │        │          │
    ▼          ▼       ▼       ▼        ▼          ▼
 Systems    Files   CRC32    SHA1    Verified    Text
 Games      Scan    MD5      MD5     Missing     CSV
 ROMs       Skip    SHA1     CRC32   Unverified  JSON
            Arch.

👾 "It's dangerous to go alone! Take this pipeline." 👾
```

---

## 🛠️ Tech Stack (The Party Members)

| Component      | Technology                        | Role                      |
| ------------- | -------------------------------  | ------------------------- |
| 💻 Language   | C++23                             | The hero of our story     |
| 🏗️ Build      | CMake 3.25+ / vcpkg               | Dungeon architect         |
| 🗄️ Database   | SQLite3 (WAL mode)                | The wizard's tome         |
| 📄 XML Parsing| pugixml                           | Scroll reader             |
| #️⃣ Hashing    | OpenSSL (MD5/SHA1) + constexpr CRC32 | The rogue           |
| 📦 Archives   | libarchive (zip/7z/tar)           | Treasure chest handler    |
| ⌨️ CLI        | CLI11                             | The bard (always talking) |
| 🎮 GUI        | ImGui + GLFW + OpenGL3            | The enchanted mirror      |
| 📝 Logging    | spdlog                            | The chronicler            |
| 🔗 JSON       | nlohmann-json                     | The translator            |
| 🧪 Testing    | Google Test                       | Quality assurance paladin |

---

## 🤝 Contributing (Join the Guild!)

Want to help shape this legendary artifact? Here's the Guild Code:

- 📜 Follow the C++ Core Guidelines (see `.github/copilot-instructions.md`)
- 💬 Conventional Commits: `feat(scope)`, `fix(scope)`, `refactor(scope)`
- ✨ Format code with `clang-format` before committing — untidy code is like a corrupted save file
- 🤖 All PRs must pass CI (MSVC + GCC + Clang) — no softlocking the build pipeline

> 🎲 *"With great power comes great `git blame`."*

---

## 🔐 Secret Konami Cheat Code

```text
↑ ↑ ↓ ↓ ← → ← → B A
```

*Doesn't actually do anything. But you tried it anyway, didn't you? Respect.* 🫡
