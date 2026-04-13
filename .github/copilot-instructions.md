# ROMULUS — Copilot Instructions

## Workflow Requirements (Repository Policy)

- **Always read the `README.md` before making any code or documentation changes.**
- **After making any changes, always update `CHANGELOG.md` and `README.md` if needed to reflect the changes.**

> These instructions apply to all code generation in this repository.
> Based on the **C++ Core Guidelines** (Stroustrup & Sutter) adapted for this project.

## Project Context

ROMULUS is a production-grade C++23 backend system for verifying and cataloging video game ROM collections using No-Intro DAT files. It uses CMake + vcpkg, targets MSVC/GCC/Clang, and enforces zero-warning builds.

**Architecture**: `lib/romulus/` (core library) → `apps/cli/` (CLI) / `apps/api/` (REST API, future) / `web/` (React frontend, future).

## Build Environment (Read Before Any Build or Dependency Task)

> **The agent environment has NO internet access.** Do not attempt to run `vcpkg install`, `vcpkg integrate`, `cmake --preset`, or any command that fetches packages or toolchains from the network. It will always fail.

- All C++ dependencies are declared in `vcpkg.json`: `sqlite3`, `spdlog`, `pugixml`, `cli11`, `libarchive`, `openssl`, `nlohmann-json`, `gtest`, `imgui` (glfw+opengl3), `glfw3`.
- Do **not** search for vcpkg, try to locate `vcpkg.exe`, or attempt to bootstrap it — assume it is unavailable.
- If asked to add a dependency, edit `vcpkg.json` only. Do not run any install commands.
- CMake presets are in `CMakePresets.json`. Do not try to configure or build — just discuss or edit source files.

---

## C++ Standard & Language

- **Use C++23**. Leverage modern features where they improve clarity:
  - `std::expected<T, E>` for error handling (not exceptions, not error codes)
  - `std::filesystem` for all path operations
  - `std::ranges` and range adaptors for collection processing
  - `constexpr` wherever possible for compile-time computation
  - `std::optional` for values that may be absent
  - `std::string_view` for non-owning string references
  - Structured bindings (`auto [key, value] = ...`)
  - `enum class` (never unscoped enums)
- **Never use**: `new`/`delete`, raw owning pointers, C-style casts, `#define` for constants, `using namespace std;`

---

## Naming Conventions

| Element | Style | Example |
|---------|-------|---------|
| Namespaces | `snake_case` | `romulus::dat`, `romulus::core` |
| Types / Classes / Structs | `PascalCase` | `DatParser`, `HashDigest`, `RomStatus` |
| Functions / Methods | `snake_case` | `compute_hashes()`, `import_dat()` |
| Variables / Parameters | `snake_case` | `file_path`, `rom_count` |
| Member variables | `snake_case_` (trailing underscore) | `db_path_`, `connection_` |
| Constants / constexpr | `k_PascalCase` | `k_MaxBufferSize`, `k_DefaultPort` |
| Enum values | `PascalCase` | `RomStatus::Verified`, `MatchType::Exact` |
| Macros (avoid!) | `UPPER_SNAKE_CASE` | `ROMULUS_VERSION` |
| Template parameters | `PascalCase` | `typename ResultType` |
| File names | `snake_case` | `dat_parser.hpp`, `hash_service.cpp` |

---

## Header & Source File Rules

- Use `#pragma once` (not include guards)
- Headers use `.hpp`, source files use `.cpp`
- Include order (separated by blank lines):
  1. Corresponding header (for `.cpp` files)
  2. Project headers (`romulus/...`)
  3. Third-party headers (`spdlog/...`, `pugixml.hpp`, etc.)
  4. Standard library headers (`<string>`, `<vector>`, etc.)
- Forward-declare in headers when possible; include in `.cpp`
- Every header must be self-contained (compilable on its own)

---

## Error Handling (CG: E, F.21)

- **Use `std::expected<T, romulus::Error>` (aliased as `Result<T>`)** for all operations that can fail
- **Never throw exceptions** in library code
- **Never ignore return values** — always check `Result` before accessing the value
- **Propagate errors early** — return on failure, don't nest deeply

```cpp
// ✅ Good
auto result = parse_dat(path);
if (!result) {
  return std::unexpected(result.error());
}
const auto& dat = result.value();

// ❌ Bad — throwing exceptions
throw std::runtime_error("parse failed");

// ❌ Bad — error codes
int parse_dat(const Path& p, DatFile& out); // returns 0 on success
```

---

## Resource Management (CG: R, C.31)

- **RAII everywhere** — acquire in constructor, release in destructor
- **Rule of Zero**: prefer classes that don't need custom destructors/copy/move
- **Rule of Five**: if you define any of dtor/copy/move, define all five
- Use `std::unique_ptr` for exclusive ownership
- Use `std::shared_ptr` only when shared ownership is genuinely required
- Use raw pointers **only** for non-owning observation (never for ownership)
- Prefer references over pointers when null is not a valid state

```cpp
// ✅ Good — RAII database handle
class Database {
public:
  explicit Database(const std::filesystem::path& path);
  ~Database(); // closes sqlite3*
  Database(Database&&) noexcept;
  Database& operator=(Database&&) noexcept;
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;
private:
  sqlite3* db_ = nullptr;   // raw ptr OK — Database owns the lifecycle via RAII
};
```

---

## Functions (CG: F)

- **Single responsibility** — one function, one job
- **Small functions** — if a function exceeds ~40 lines, consider splitting
- Pass parameters by:
  - `const T&` — for read-only input (large types)
  - `T` (by value) — for small types (int, enum, string_view) or when you need a copy
  - `T&` — for in-out parameters (avoid when possible; prefer return values)
  - `T&&` — for sink parameters (the function will consume/move from it)
- **Return values over out-parameters** — use `Result<T>`, `std::optional<T>`, structured bindings
- Mark functions `[[nodiscard]]` when ignoring the return value is likely a bug
- Mark functions `noexcept` when they cannot throw

```cpp
// ✅ Good
[[nodiscard]] auto compute_hashes(const std::filesystem::path& file_path) -> Result<HashDigest>;

// ❌ Bad — out-parameter style
bool compute_hashes(const std::filesystem::path& path, HashDigest* out);
```

---

## Classes & Structs (CG: C)

- Use `struct` for passive data aggregates (all public, no invariants)
- Use `class` when there are invariants to maintain (private data + public interface)
- Mark single-argument constructors `explicit`
- Mark classes not designed for inheritance as `final`
- Prefer composition over inheritance
- Keep class interfaces minimal — don't add "just in case" methods

```cpp
// ✅ Struct for data
struct HashDigest {
  std::string crc32;
  std::string md5;
  std::string sha1;
};

// ✅ Class for behavior with invariants
class DatParser final {
public:
  [[nodiscard]] auto parse(const std::filesystem::path& dat_path) -> Result<DatFile>;
private:
  auto parse_header(pugi::xml_node node) -> Result<DatHeader>;
  auto parse_game(pugi::xml_node node) -> Result<GameInfo>;
};
```

---

## Constants & Immutability (CG: Con)

- **Default to `const`** — make everything `const` unless mutation is required
- Use `constexpr` for compile-time constants
- Use `const` for runtime constants
- Prefer `std::string_view` over `const std::string&` for string parameters that won't be stored

```cpp
constexpr std::size_t k_HashBufferSize = 65536;     // 64KB
constexpr std::string_view k_AppName = "romulus";
```

---

## Containers & Algorithms (CG: SL, ES.1)

- **Prefer STL algorithms + ranges over raw loops**
- Use `std::vector` as the default container
- Use `std::unordered_map` / `std::unordered_set` when order doesn't matter and you need O(1) lookup
- Use `std::map` / `std::set` only when ordered iteration is needed
- Reserve capacity when the size is known in advance

```cpp
// ✅ Good — ranges
auto missing = roms | std::views::filter([](const auto& r) {
  return r.status == RomStatus::Missing;
});

// ❌ Bad — raw loop with index
for (int i = 0; i < roms.size(); ++i) { ... }
```

---

## Concurrency (CG: CP)

- **No global mutable state**
- Guard shared data with `std::mutex` + `std::lock_guard` / `std::scoped_lock`
- Prefer task-based concurrency (`std::async`, `std::jthread`) over raw threads
- SQLite operations must be serialized — use a single `Database` instance per thread

---

## Logging

- Use `spdlog` via the project's logging facade (`romulus/core/logging.hpp`)
- Log levels: `SPDLOG_TRACE`, `SPDLOG_DEBUG`, `SPDLOG_INFO`, `SPDLOG_WARN`, `SPDLOG_ERROR`, `SPDLOG_CRITICAL`
- Log **what happened and why** — not just "error occurred"
- Include relevant context (file path, ROM name, hash values) in log messages
- Never log sensitive file paths in production without sanitization

```cpp
spdlog::info("Imported DAT: system='{}', version='{}', games={}",
             dat.system_name, dat.version, dat.games.size());
spdlog::error("Hash mismatch for '{}': expected={}, got={}",
              rom_name, expected_sha1, actual_sha1);
```

---

## Database Conventions

- **All DB writes must be transactional** — use the RAII `TransactionGuard`
- **Use prepared statements** — never build SQL by string concatenation
- **Batch operations** — group inserts in transactions of 100+ rows
- **Idempotent migrations** — always use `CREATE TABLE IF NOT EXISTS`
- **Normalize** — reference IDs, not duplicate strings
- **ISO 8601 timestamps** — store dates as `TEXT` in `datetime('now')` format

---

## CMake Conventions

- One `CMakeLists.txt` per module directory
- Use `target_*` commands (not global `include_directories`, `link_libraries`)
- Never pollute the global scope
- Use `target_compile_features(target PUBLIC cxx_std_23)`
- Test targets are conditional: `if(BUILD_TESTING)`
- All third-party dependencies come through vcpkg — never vendor or `FetchContent`

---

## Code Formatting

- Enforced by `.clang-format` (LLVM-based style)
- Maximum line width: **100 characters**
- Braces: **K&R style** (opening brace on same line)
- Indentation: **2 spaces** (no tabs)
- Always use braces for `if`/`else`/`for`/`while` — even single-line bodies
- Pointer/reference: `int* ptr` (left-aligned)

---

## Namespace Rules

- All library code lives under `namespace romulus { ... }`
- Sub-namespaces match directory: `romulus::core`, `romulus::dat`, `romulus::database`, `romulus::scanner`, `romulus::engine`, `romulus::report`, `romulus::service`
- Never `using namespace` in headers
- `using namespace` is acceptable in `.cpp` files, scoped within functions

---

## Documentation

- Public API functions/classes must have Doxygen-style `///` comments
- Explain **why**, not **what** — the code should be self-explanatory for "what"
- Mark TODO/FIXME/HACK with the format: `// TODO(username): description`
- Non-obvious algorithms must have a brief explanation

```cpp
/// Computes CRC32, MD5, and SHA1 hashes in a single pass over the file.
/// Uses a 64KB read buffer to minimize I/O syscalls.
/// @param file_path Absolute path to the file to hash.
/// @return HashDigest on success, or Error if the file cannot be read.
[[nodiscard]] auto compute_hashes(const std::filesystem::path& file_path) -> Result<HashDigest>;
```

---

## Testing

- Test file mirrors source: `lib/romulus/dat/dat_parser.cpp` → `tests/unit/test_dat_parser.cpp`
- Use Google Test (`TEST`, `TEST_F`, `EXPECT_*`, `ASSERT_*`)
- Test names: `TEST(ModuleName, descriptive_behavior_being_tested)`
- Each test should be independent — no shared mutable state between tests
- Use fixtures (`test/fixtures/`) for deterministic test data

```cpp
TEST(DatParser, parses_valid_logiqx_xml_with_single_game) { ... }
TEST(DatParser, returns_error_for_malformed_xml) { ... }
TEST(HashService, computes_correct_sha1_for_known_file) { ... }
```

---

## Git & Commit Conventions

- Commit messages: `type(scope): description`
  - Types: `feat`, `fix`, `refactor`, `test`, `docs`, `build`, `chore`
  - Scope: module name (`dat`, `scanner`, `database`, `core`, `cli`, `cmake`)
  - Example: `feat(dat): implement LogiqX XML parser`
- One logical change per commit
- Keep CHANGELOG.md updated (Keep a Changelog format)
- When making repository changes, update CHANGELOG.md unless explicitly told not to

---

## What NOT to Do

- ❌ `new` / `delete` — use RAII and smart pointers
- ❌ C-style casts `(int)x` — use `static_cast<int>(x)`
- ❌ `#define` for constants — use `constexpr`
- ❌ `using namespace std;` — especially in headers
- ❌ Unscoped enums `enum Color { Red }` — use `enum class`
- ❌ `std::endl` — use `'\n'` (endl forces a flush)
- ❌ Hungarian notation `strName`, `iCount` — use descriptive names
- ❌ Single-letter variables (except loop counters `i`, `j`, `k`)
- ❌ Magic numbers — name them as `constexpr`
- ❌ Commented-out code — delete it, git remembers
- ❌ Global mutable state
- ❌ Implicit conversions — mark constructors `explicit`
