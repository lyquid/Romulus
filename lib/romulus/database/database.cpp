#include "romulus/database/database.hpp"

#include "romulus/core/logging.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace romulus::database {

namespace {

auto hex_to_bytes(std::string_view hex) -> std::vector<uint8_t> {
  std::vector<uint8_t> bytes;
  if (hex.empty()) {
    return bytes;
  }
  // Reject odd-length or non-hex input
  if (hex.length() % 2 != 0) {
    return bytes;
  }
  bytes.reserve(hex.length() / 2);
  for (size_t i = 0; i < hex.length(); i += 2) {
    const char hi = hex[i];
    const char lo = hex[i + 1];
    auto is_hex = [](char c) {
      return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    };
    if (!is_hex(hi) || !is_hex(lo)) {
      return {};
    }
    auto byteString = std::string(hex.substr(i, 2));
    bytes.push_back(static_cast<uint8_t>(strtol(byteString.c_str(), nullptr, 16)));
  }
  return bytes;
}

auto bytes_to_hex(const std::vector<uint8_t>& bytes) -> std::string {
  static constexpr char hex_chars[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(bytes.size() * 2);
  for (const auto b : bytes) {
    hex.push_back(hex_chars[b >> 4]);
    hex.push_back(hex_chars[b & 0x0F]);
  }
  return hex;
}

/// Wraps an SQLite identifier in double-quotes and escapes any embedded
/// double-quote characters by doubling them (SQLite quoting rules).
/// Example: my"table → "my""table"
[[nodiscard]] auto quote_identifier(const std::string& name) -> std::string {
  std::string out;
  out.reserve(name.size() + 2U);
  out += '"';
  for (const char c : name) {
    out += c;
    if (c == '"') {
      out += '"'; // double embedded quotes
    }
  }
  out += '"';
  return out;
}

} // namespace

// ═══════════════════════════════════════════════════════════════
// PreparedStatement
// ═══════════════════════════════════════════════════════════════

PreparedStatement::PreparedStatement(sqlite3_stmt* stmt) : stmt_(stmt) {}

PreparedStatement::~PreparedStatement() {
  if (stmt_ != nullptr) {
    sqlite3_finalize(stmt_);
  }
}

PreparedStatement::PreparedStatement(PreparedStatement&& other) noexcept
    : stmt_(std::exchange(other.stmt_, nullptr)) {}

auto PreparedStatement::operator=(PreparedStatement&& other) noexcept -> PreparedStatement& {
  if (this != &other) {
    if (stmt_ != nullptr) {
      sqlite3_finalize(stmt_);
    }
    stmt_ = std::exchange(other.stmt_, nullptr);
  }
  return *this;
}

void PreparedStatement::bind_int64(int index, std::int64_t value) {
  sqlite3_bind_int64(stmt_, index, value);
}

void PreparedStatement::bind_text(int index, std::string_view value) {
  sqlite3_bind_text(stmt_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

void PreparedStatement::bind_blob(int index, const std::vector<uint8_t>& blob) {
  if (blob.empty()) {
    sqlite3_bind_null(stmt_, index);
  } else {
    sqlite3_bind_blob(stmt_, index, blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);
  }
}

void PreparedStatement::bind_null(int index) {
  sqlite3_bind_null(stmt_, index);
}

auto PreparedStatement::step() -> bool {
  int rc = sqlite3_step(stmt_);
  if (rc == SQLITE_ROW) {
    return true;
  }
  if (rc == SQLITE_DONE) {
    return false;
  }
  throw std::runtime_error(std::string("SQLite step error: ") +
                           sqlite3_errmsg(sqlite3_db_handle(stmt_)));
}

void PreparedStatement::execute() {
  [[maybe_unused]] bool result = step();
}

void PreparedStatement::reset() {
  sqlite3_reset(stmt_);
  sqlite3_clear_bindings(stmt_);
}

auto PreparedStatement::column_int64(int index) const -> std::int64_t {
  return sqlite3_column_int64(stmt_, index);
}

auto PreparedStatement::column_text(int index) const -> std::string {
  const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, index));
  if (text == nullptr) {
    return {};
  }
  return std::string(text);
}

auto PreparedStatement::column_optional_text(int index) const -> std::optional<std::string> {
  if (sqlite3_column_type(stmt_, index) == SQLITE_NULL) {
    return std::nullopt;
  }
  return column_text(index);
}

auto PreparedStatement::column_blob(int index) const -> std::vector<uint8_t> {
  if (sqlite3_column_type(stmt_, index) == SQLITE_NULL) {
    return {};
  }
  const void* blob = sqlite3_column_blob(stmt_, index);
  int bytes = sqlite3_column_bytes(stmt_, index);
  if (!blob || bytes == 0) {
    return {};
  }
  const auto* p = static_cast<const uint8_t*>(blob);
  return std::vector<uint8_t>(p, p + bytes);
}

auto PreparedStatement::column_display_text(int index) const -> std::string {
  const int col_type = sqlite3_column_type(stmt_, index);
  if (col_type == SQLITE_NULL) {
    return "(NULL)";
  }
  if (col_type == SQLITE_BLOB) {
    // Render raw binary data as a lowercase hex string.
    const void* blob = sqlite3_column_blob(stmt_, index);
    const int bytes = sqlite3_column_bytes(stmt_, index);
    if (!blob || bytes == 0) {
      return "";
    }
    static constexpr char k_HexChars[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(static_cast<std::size_t>(bytes) * 2);
    const auto* p = static_cast<const uint8_t*>(blob);
    for (int i = 0; i < bytes; ++i) {
      hex.push_back(k_HexChars[p[i] >> 4U]);
      hex.push_back(k_HexChars[p[i] & 0x0FU]);
    }
    return hex;
  }
  // SQLITE_INTEGER, SQLITE_FLOAT, SQLITE_TEXT — readable via sqlite3_column_text.
  const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, index));
  return text ? std::string(text) : "";
}

// ═══════════════════════════════════════════════════════════════
// TransactionGuard
// ═══════════════════════════════════════════════════════════════

TransactionGuard::TransactionGuard(sqlite3* db) : db_(db) {
}

TransactionGuard::~TransactionGuard() {
  if (db_ != nullptr && !committed_) {
    sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    ROMULUS_WARN("Transaction rolled back (not explicitly committed)");
  }
}

TransactionGuard::TransactionGuard(TransactionGuard&& other) noexcept
    : db_(std::exchange(other.db_, nullptr)), committed_(other.committed_) {}

auto TransactionGuard::operator=(TransactionGuard&& other) noexcept -> TransactionGuard& {
  if (this != &other) {
    if (db_ != nullptr && !committed_) {
      sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    db_ = std::exchange(other.db_, nullptr);
    committed_ = other.committed_;
  }
  return *this;
}

auto TransactionGuard::commit() -> Result<void> {
  if (db_ != nullptr && !committed_) {
    char* err_msg = nullptr;
    const int rc = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
      std::string err = err_msg != nullptr ? err_msg : "unknown error";
      sqlite3_free(err_msg);
      return std::unexpected(
          core::Error{core::ErrorCode::DatabaseQueryError, "COMMIT failed: " + err});
    }
    committed_ = true;
  }
  return {};
}

auto TransactionGuard::rollback() -> Result<void> {
  if (db_ != nullptr && !committed_) {
    char* err_msg = nullptr;
    const int rc = sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
      std::string err = err_msg != nullptr ? err_msg : "unknown error";
      sqlite3_free(err_msg);
      return std::unexpected(
          core::Error{core::ErrorCode::DatabaseQueryError, "ROLLBACK failed: " + err});
    }
    committed_ = true;
  }
  return {};
}

// ═══════════════════════════════════════════════════════════════
// Database — Construction & Configuration
// ═══════════════════════════════════════════════════════════════

namespace {

constexpr std::string_view k_Schema = R"SQL(
CREATE TABLE IF NOT EXISTS dat_versions (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    name          TEXT NOT NULL,
    version       TEXT NOT NULL,
    system        TEXT,
    source_url    TEXT,
    dat_sha256    TEXT NOT NULL,
    imported_at   TEXT NOT NULL DEFAULT (datetime('now', 'localtime')),
    UNIQUE(dat_sha256)
);

-- Games are the normalized parent entities from DAT files.
-- Each game belongs to exactly one DAT version and may contain multiple ROMs.
CREATE TABLE IF NOT EXISTS games (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    dat_version_id  INTEGER NOT NULL REFERENCES dat_versions(id),
    name            TEXT NOT NULL,
    UNIQUE(dat_version_id, name)
);

-- ROMs are the expected entries from DAT files, linked to their parent game.
-- expected_sha1 is the authoritative hash declared by the DAT, stored as BLOB(20).
-- All hash columns are BLOB for uniform storage (no mixed TEXT/BLOB chaos).
CREATE TABLE IF NOT EXISTS roms (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    game_id         INTEGER NOT NULL REFERENCES games(id),
    name            TEXT NOT NULL,
    size            INTEGER,
    crc32           BLOB,
    md5             BLOB,
    expected_sha1   BLOB,
    sha256          BLOB,
    region          TEXT
);

CREATE TABLE IF NOT EXISTS global_roms (
    sha1          BLOB PRIMARY KEY,
    sha256        BLOB,
    md5           BLOB,
    crc32         BLOB,
    size          INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS files (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    path            TEXT NOT NULL COLLATE NOCASE,
    archive_path    TEXT,
    entry_name      TEXT,
    size            INTEGER NOT NULL,
    crc32           BLOB,
    md5             BLOB,
    sha1            BLOB NOT NULL REFERENCES global_roms(sha1),
    sha256          BLOB,
    last_scanned    INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    last_write_time INTEGER NOT NULL DEFAULT 0,
    UNIQUE(path)
);

CREATE TABLE IF NOT EXISTS rom_matches (
    rom_id          INTEGER NOT NULL REFERENCES roms(id),
    global_rom_sha1 BLOB NOT NULL REFERENCES global_roms(sha1),
    match_type      INTEGER NOT NULL,
    PRIMARY KEY (rom_id, global_rom_sha1)
);

-- Persists ROM scan directory paths across application sessions.
-- Users register directories via the Folders tab; they are rescanned on demand.
CREATE TABLE IF NOT EXISTS scanned_directories (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    path      TEXT NOT NULL UNIQUE,
    added_at  TEXT NOT NULL DEFAULT (datetime('now', 'localtime'))
);

-- Indexes for fast hash lookups
CREATE INDEX IF NOT EXISTS idx_games_dat_version ON games(dat_version_id);
CREATE INDEX IF NOT EXISTS idx_roms_game ON roms(game_id);
CREATE INDEX IF NOT EXISTS idx_roms_expected_sha1 ON roms(expected_sha1);
CREATE INDEX IF NOT EXISTS idx_roms_md5 ON roms(md5);
CREATE INDEX IF NOT EXISTS idx_roms_crc32 ON roms(crc32);
CREATE UNIQUE INDEX IF NOT EXISTS idx_roms_sha256 ON roms(sha256) WHERE sha256 IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_files_sha1 ON files(sha1);
CREATE INDEX IF NOT EXISTS idx_files_crc32 ON files(crc32);
CREATE INDEX IF NOT EXISTS idx_files_sha256 ON files(sha256);
CREATE INDEX IF NOT EXISTS idx_global_roms_md5 ON global_roms(md5) WHERE md5 IS NOT NULL;
-- Hot path: look up matches by file sha1 (global_rom_sha1)
CREATE INDEX IF NOT EXISTS idx_rom_matches_sha1 ON rom_matches(global_rom_sha1);
)SQL";

/// Schema version — increment whenever the schema changes in a backward-incompatible way.
/// Stored in PRAGMA user_version. If the on-disk DB has a different version the database
/// is wiped and rebuilt so queries never encounter stale column layouts.
constexpr int k_SchemaVersion = 6;

auto match_type_to_int(core::MatchType type) -> int {
  switch (type) {
    case core::MatchType::Exact:
      return 0;
    case core::MatchType::Sha256Only:
      return 1;
    case core::MatchType::Sha1Only:
      return 2;
    case core::MatchType::Md5Only:
      return 3;
    case core::MatchType::Crc32Only:
      return 4;
    case core::MatchType::NoMatch:
      return 5;
  }
  return 5;
}

auto int_to_match_type(int value) -> core::MatchType {
  switch (value) {
    case 0:
      return core::MatchType::Exact;
    case 1:
      return core::MatchType::Sha256Only;
    case 2:
      return core::MatchType::Sha1Only;
    case 3:
      return core::MatchType::Md5Only;
    case 4:
      return core::MatchType::Crc32Only;
    default:
      return core::MatchType::NoMatch;
  }
}

} // namespace

Database::Database(const std::filesystem::path& db_path) {
  auto path_str = db_path.string();
  int rc = sqlite3_open(path_str.c_str(), &db_);
  if (rc != SQLITE_OK) {
    std::string err = db_ != nullptr ? sqlite3_errmsg(db_) : "unknown error";
    if (db_ != nullptr) {
      sqlite3_close(db_);
      db_ = nullptr;
    }
    throw std::runtime_error("Failed to open database '" + path_str + "': " + err);
  }

  configure_connection();
  run_migrations();

  ROMULUS_INFO("Database opened: {}", path_str);
}

Database::~Database() {
  if (db_ != nullptr) {
    sqlite3_close(db_);
  }
}

Database::Database(Database&& other) noexcept : db_(std::exchange(other.db_, nullptr)) {}

auto Database::operator=(Database&& other) noexcept -> Database& {
  if (this != &other) {
    if (db_ != nullptr) {
      sqlite3_close(db_);
    }
    db_ = std::exchange(other.db_, nullptr);
  }
  return *this;
}

void Database::configure_connection() {
  // WAL mode for concurrent reads
  sqlite3_exec(db_, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
  // Enforce foreign keys
  sqlite3_exec(db_, "PRAGMA foreign_keys=ON", nullptr, nullptr, nullptr);
  // Synchronous NORMAL for balance of safety and speed
  sqlite3_exec(db_, "PRAGMA synchronous=NORMAL", nullptr, nullptr, nullptr);
}

void Database::run_migrations() {
  // Read the on-disk schema version.
  int on_disk_version = 0;
  sqlite3_exec(
      db_, "PRAGMA user_version",
      [](void* out, int, char** cols, char**) -> int {
        *static_cast<int*>(out) = cols[0] != nullptr ? std::stoi(cols[0]) : 0;
        return 0;
      },
      &on_disk_version, nullptr);

  if (on_disk_version != 0 && on_disk_version != k_SchemaVersion) {
    // Schema has changed — wipe all application tables so stale column layouts
    // never cause runtime errors.  User data is intentionally discarded here;
    // a fresh import / rescan is required after upgrading.
    ROMULUS_WARN(
        "Database schema version mismatch (on-disk={}, expected={}) — recreating schema.",
        on_disk_version, k_SchemaVersion);
    constexpr std::string_view k_DropAll = R"SQL(
DROP TABLE IF EXISTS rom_matches;
DROP TABLE IF EXISTS files;
DROP TABLE IF EXISTS global_roms;
DROP TABLE IF EXISTS roms;
DROP TABLE IF EXISTS games;
DROP TABLE IF EXISTS dat_versions;
DROP TABLE IF EXISTS scanned_directories;
DROP TABLE IF EXISTS systems;
DROP TABLE IF EXISTS rom_status;
)SQL";
    char* drop_err = nullptr;
    sqlite3_exec(db_, std::string(k_DropAll).c_str(), nullptr, nullptr, &drop_err);
    if (drop_err != nullptr) {
      sqlite3_free(drop_err);
    }
  }

  // Apply the main schema (CREATE TABLE/INDEX IF NOT EXISTS — safe to run on any DB state).
  char* err_msg = nullptr;
  int rc = sqlite3_exec(db_, std::string(k_Schema).c_str(), nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    std::string err = err_msg != nullptr ? err_msg : "unknown error";
    sqlite3_free(err_msg);
    throw std::runtime_error("Migration failed: " + err);
  }

  // Stamp the version so future opens can detect incompatible schema changes.
  if (on_disk_version != k_SchemaVersion) {
    std::string set_version =
        "PRAGMA user_version = " + std::to_string(k_SchemaVersion) + ";";
    sqlite3_exec(db_, set_version.c_str(), nullptr, nullptr, nullptr);
  }

  ROMULUS_DEBUG("Database migrations applied successfully (schema_version={})", k_SchemaVersion);
}

auto Database::begin_transaction() -> Result<TransactionGuard> {
  char* err_msg = nullptr;
  const int rc = sqlite3_exec(db_, "BEGIN TRANSACTION", nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    std::string err = err_msg != nullptr ? err_msg : "unknown error";
    sqlite3_free(err_msg);
    return std::unexpected(
        core::Error{core::ErrorCode::DatabaseQueryError, "BEGIN TRANSACTION failed: " + err});
  }

  return TransactionGuard{db_};
}

auto Database::execute(std::string_view sql) -> Result<void> {
  char* err_msg = nullptr;
  int rc = sqlite3_exec(db_, std::string(sql).c_str(), nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    std::string err = err_msg != nullptr ? err_msg : "unknown error";
    sqlite3_free(err_msg);
    return std::unexpected(core::Error{core::ErrorCode::DatabaseQueryError, "SQL error: " + err});
  }
  return {};
}

auto Database::prepare(std::string_view sql) -> Result<PreparedStatement> {
  sqlite3_stmt* stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
  if (rc != SQLITE_OK) {
    return std::unexpected(core::Error{core::ErrorCode::DatabaseQueryError,
                                       std::string("Prepare failed: ") + sqlite3_errmsg(db_)});
  }
  return PreparedStatement{stmt};
}

auto Database::last_insert_id() const -> std::int64_t {
  return sqlite3_last_insert_rowid(db_);
}

// ═══════════════════════════════════════════════════════════════
// DAT Versions CRUD
// ═══════════════════════════════════════════════════════════════

auto Database::insert_dat_version(const core::DatVersion& dat) -> Result<std::int64_t> {
  auto stmt = prepare(
      "INSERT INTO dat_versions (name, version, system, source_url, dat_sha256, imported_at) "
      "VALUES (?1, ?2, ?3, ?4, ?5, datetime('now', 'localtime'))");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_text(1, dat.name);
  stmt->bind_text(2, dat.version);
  if (dat.system.empty()) {
    stmt->bind_null(3);
  } else {
    stmt->bind_text(3, dat.system);
  }
  stmt->bind_text(4, dat.source_url);
  stmt->bind_text(5, dat.dat_sha256);
  stmt->execute();

  return last_insert_id();
}

auto Database::find_dat_version(std::string_view name, std::string_view version)
    -> Result<std::optional<core::DatVersion>> {
  auto stmt = prepare("SELECT id, name, version, system, source_url, dat_sha256, imported_at "
                      "FROM dat_versions WHERE name = ?1 AND version = ?2 "
                      "ORDER BY imported_at DESC, id DESC LIMIT 1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_text(1, name);
  stmt->bind_text(2, version);
  if (!stmt->step()) {
    return std::nullopt;
  }

  return core::DatVersion{
      .id = stmt->column_int64(0),
      .name = stmt->column_text(1),
      .version = stmt->column_text(2),
      .system = stmt->column_text(3),
      .source_url = stmt->column_text(4),
      .dat_sha256 = stmt->column_text(5),
      .imported_at = stmt->column_text(6),
  };
}

auto Database::find_dat_version_by_sha256(std::string_view dat_sha256)
    -> Result<std::optional<core::DatVersion>> {
  auto stmt = prepare("SELECT id, name, version, system, source_url, dat_sha256, imported_at "
                      "FROM dat_versions WHERE dat_sha256 = ?1 LIMIT 1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_text(1, dat_sha256);
  if (!stmt->step()) {
    return std::nullopt;
  }

  return core::DatVersion{
      .id = stmt->column_int64(0),
      .name = stmt->column_text(1),
      .version = stmt->column_text(2),
      .system = stmt->column_text(3),
      .source_url = stmt->column_text(4),
      .dat_sha256 = stmt->column_text(5),
      .imported_at = stmt->column_text(6),
  };
}

auto Database::find_dat_version_by_name(std::string_view name)
    -> Result<std::optional<core::DatVersion>> {
  // Returns the most recently imported DAT with the given name (most recent first).
  auto stmt = prepare("SELECT id, name, version, system, source_url, dat_sha256, imported_at "
                      "FROM dat_versions WHERE name = ?1 ORDER BY imported_at DESC LIMIT 1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_text(1, name);
  if (!stmt->step()) {
    return std::nullopt;
  }

  return core::DatVersion{
      .id = stmt->column_int64(0),
      .name = stmt->column_text(1),
      .version = stmt->column_text(2),
      .system = stmt->column_text(3),
      .source_url = stmt->column_text(4),
      .dat_sha256 = stmt->column_text(5),
      .imported_at = stmt->column_text(6),
  };
}


auto Database::get_all_dat_versions() -> Result<std::vector<core::DatVersion>> {
  auto stmt = prepare("SELECT id, name, version, system, source_url, dat_sha256, imported_at "
                      "FROM dat_versions ORDER BY imported_at DESC");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  std::vector<core::DatVersion> versions;
  while (stmt->step()) {
    versions.push_back({
        .id = stmt->column_int64(0),
        .name = stmt->column_text(1),
        .version = stmt->column_text(2),
        .system = stmt->column_text(3),
        .source_url = stmt->column_text(4),
        .dat_sha256 = stmt->column_text(5),
        .imported_at = stmt->column_text(6),
    });
  }
  return versions;
}

// ═══════════════════════════════════════════════════════════════
// Games CRUD
// ═══════════════════════════════════════════════════════════════

auto Database::find_or_insert_game(std::int64_t dat_version_id, std::string_view name)
    -> Result<std::int64_t> {
  // Use INSERT OR IGNORE so concurrent inserts are safe, then fetch the real id.
  auto ins = prepare("INSERT OR IGNORE INTO games (dat_version_id, name) VALUES (?1, ?2)");
  if (!ins) {
    return std::unexpected(ins.error());
  }
  ins->bind_int64(1, dat_version_id);
  ins->bind_text(2, name);
  ins->execute();

  auto sel = prepare("SELECT id FROM games WHERE dat_version_id = ?1 AND name = ?2 LIMIT 1");
  if (!sel) {
    return std::unexpected(sel.error());
  }
  sel->bind_int64(1, dat_version_id);
  sel->bind_text(2, name);
  if (!sel->step()) {
    return std::unexpected(
        core::Error{core::ErrorCode::DatabaseQueryError, "Game not found after insert"});
  }
  return sel->column_int64(0);
}

auto Database::get_games_for_dat_version(std::int64_t dat_version_id)
    -> Result<std::vector<core::GameEntry>> {
  auto stmt = prepare("SELECT id, dat_version_id, name FROM games "
                      "WHERE dat_version_id = ?1 ORDER BY name");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }
  stmt->bind_int64(1, dat_version_id);

  std::vector<core::GameEntry> games;
  while (stmt->step()) {
    games.push_back({
        .id = stmt->column_int64(0),
        .dat_version_id = stmt->column_int64(1),
        .name = stmt->column_text(2),
    });
  }
  return games;
}

auto Database::get_roms_for_dat_version(std::int64_t dat_version_id)
    -> Result<std::vector<core::RomInfo>> {
  auto stmt = prepare(
      "SELECT r.id, r.game_id, g.dat_version_id, g.name, r.name, r.size, "
      "r.crc32, r.md5, r.expected_sha1, r.sha256, r.region "
      "FROM roms r "
      "JOIN games g ON r.game_id = g.id "
      "WHERE g.dat_version_id = ?1 ORDER BY r.name");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_int64(1, dat_version_id);

  std::vector<core::RomInfo> roms;
  while (stmt->step()) {
    roms.push_back({
        .id = stmt->column_int64(0),
        .game_id = stmt->column_int64(1),
        .name = stmt->column_text(4),
        .size = stmt->column_int64(5),
        .crc32 = bytes_to_hex(stmt->column_blob(6)),
        .md5 = bytes_to_hex(stmt->column_blob(7)),
        .sha1 = bytes_to_hex(stmt->column_blob(8)),
        .sha256 = bytes_to_hex(stmt->column_blob(9)),
        .region = stmt->column_text(10),
        .dat_version_id = stmt->column_int64(2),
        .game_name = stmt->column_text(3),
    });
  }
  return roms;
}

// ═══════════════════════════════════════════════════════════════
// ROMs CRUD
// ═══════════════════════════════════════════════════════════════

auto Database::insert_rom(const core::RomInfo& rom) -> Result<std::int64_t> {
  auto stmt =
      prepare("INSERT INTO roms (game_id, name, size, crc32, md5, "
              "expected_sha1, sha256, region) "
              "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_int64(1, rom.game_id);
  stmt->bind_text(2, rom.name);
  stmt->bind_int64(3, rom.size);
  stmt->bind_blob(4, hex_to_bytes(rom.crc32));
  stmt->bind_blob(5, hex_to_bytes(rom.md5));
  stmt->bind_blob(6, hex_to_bytes(rom.sha1));
  if (rom.sha256.empty()) {
    stmt->bind_null(7);
  } else {
    stmt->bind_blob(7, hex_to_bytes(rom.sha256));
  }
  stmt->bind_text(8, rom.region);
  stmt->execute();

  return last_insert_id();
}

auto Database::find_rom_by_sha1(std::string_view sha1) -> Result<std::optional<core::RomInfo>> {
  auto stmt =
      prepare("SELECT r.id, r.game_id, g.dat_version_id, g.name, r.name, r.size, "
              "r.crc32, r.md5, r.expected_sha1, r.sha256, r.region "
              "FROM roms r "
              "JOIN games g ON r.game_id = g.id "
              "WHERE r.expected_sha1 = ?1 LIMIT 1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_blob(1, hex_to_bytes(sha1));
  if (!stmt->step()) {
    return std::nullopt;
  }

  return core::RomInfo{
      .id = stmt->column_int64(0),
      .game_id = stmt->column_int64(1),
      .name = stmt->column_text(4),
      .size = stmt->column_int64(5),
      .crc32 = bytes_to_hex(stmt->column_blob(6)),
      .md5 = bytes_to_hex(stmt->column_blob(7)),
      .sha1 = bytes_to_hex(stmt->column_blob(8)),
      .sha256 = bytes_to_hex(stmt->column_blob(9)),
      .region = stmt->column_text(10),
      .dat_version_id = stmt->column_int64(2),
      .game_name = stmt->column_text(3),
  };
}

auto Database::find_rom_by_sha256(std::string_view sha256)
    -> Result<std::optional<core::RomInfo>> {
  auto stmt =
      prepare("SELECT r.id, r.game_id, g.dat_version_id, g.name, r.name, r.size, "
              "r.crc32, r.md5, r.expected_sha1, r.sha256, r.region "
              "FROM roms r "
              "JOIN games g ON r.game_id = g.id "
              "WHERE r.sha256 = ?1 LIMIT 1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_blob(1, hex_to_bytes(sha256));
  if (!stmt->step()) {
    return std::nullopt;
  }

  return core::RomInfo{
      .id = stmt->column_int64(0),
      .game_id = stmt->column_int64(1),
      .name = stmt->column_text(4),
      .size = stmt->column_int64(5),
      .crc32 = bytes_to_hex(stmt->column_blob(6)),
      .md5 = bytes_to_hex(stmt->column_blob(7)),
      .sha1 = bytes_to_hex(stmt->column_blob(8)),
      .sha256 = bytes_to_hex(stmt->column_blob(9)),
      .region = stmt->column_text(10),
      .dat_version_id = stmt->column_int64(2),
      .game_name = stmt->column_text(3),
  };
}

auto Database::find_rom_by_md5(std::string_view md5) -> Result<std::optional<core::RomInfo>> {
  auto stmt =
      prepare("SELECT r.id, r.game_id, g.dat_version_id, g.name, r.name, r.size, "
              "r.crc32, r.md5, r.expected_sha1, r.sha256, r.region "
              "FROM roms r "
              "JOIN games g ON r.game_id = g.id "
              "WHERE r.md5 = ?1 LIMIT 1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_blob(1, hex_to_bytes(md5));
  if (!stmt->step()) {
    return std::nullopt;
  }

  return core::RomInfo{
      .id = stmt->column_int64(0),
      .game_id = stmt->column_int64(1),
      .name = stmt->column_text(4),
      .size = stmt->column_int64(5),
      .crc32 = bytes_to_hex(stmt->column_blob(6)),
      .md5 = bytes_to_hex(stmt->column_blob(7)),
      .sha1 = bytes_to_hex(stmt->column_blob(8)),
      .sha256 = bytes_to_hex(stmt->column_blob(9)),
      .region = stmt->column_text(10),
      .dat_version_id = stmt->column_int64(2),
      .game_name = stmt->column_text(3),
  };
}

auto Database::find_rom_by_crc32(std::string_view crc32) -> Result<std::vector<core::RomInfo>> {
  auto stmt =
      prepare("SELECT r.id, r.game_id, g.dat_version_id, g.name, r.name, r.size, "
              "r.crc32, r.md5, r.expected_sha1, r.sha256, r.region "
              "FROM roms r "
              "JOIN games g ON r.game_id = g.id "
              "WHERE r.crc32 = ?1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_blob(1, hex_to_bytes(crc32));

  std::vector<core::RomInfo> roms;
  while (stmt->step()) {
    roms.push_back({
        .id = stmt->column_int64(0),
        .game_id = stmt->column_int64(1),
        .name = stmt->column_text(4),
        .size = stmt->column_int64(5),
        .crc32 = bytes_to_hex(stmt->column_blob(6)),
        .md5 = bytes_to_hex(stmt->column_blob(7)),
        .sha1 = bytes_to_hex(stmt->column_blob(8)),
        .sha256 = bytes_to_hex(stmt->column_blob(9)),
        .region = stmt->column_text(10),
        .dat_version_id = stmt->column_int64(2),
        .game_name = stmt->column_text(3),
    });
  }
  return roms;
}

auto Database::get_all_roms() -> Result<std::vector<core::RomInfo>> {
  auto stmt =
      prepare("SELECT r.id, r.game_id, g.dat_version_id, g.name, r.name, r.size, "
              "r.crc32, r.md5, r.expected_sha1, r.sha256, r.region "
              "FROM roms r "
              "JOIN games g ON r.game_id = g.id "
              "ORDER BY g.dat_version_id, r.name");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  std::vector<core::RomInfo> roms;
  while (stmt->step()) {
    roms.push_back({
        .id = stmt->column_int64(0),
        .game_id = stmt->column_int64(1),
        .name = stmt->column_text(4),
        .size = stmt->column_int64(5),
        .crc32 = bytes_to_hex(stmt->column_blob(6)),
        .md5 = bytes_to_hex(stmt->column_blob(7)),
        .sha1 = bytes_to_hex(stmt->column_blob(8)),
        .sha256 = bytes_to_hex(stmt->column_blob(9)),
        .region = stmt->column_text(10),
        .dat_version_id = stmt->column_int64(2),
        .game_name = stmt->column_text(3),
    });
  }
  return roms;
}

// ═══════════════════════════════════════════════════════════════
// Files CRUD
// ═══════════════════════════════════════════════════════════════

auto Database::upsert_file(const core::FileInfo& file) -> Result<std::int64_t> {
  // 1. Maintain the Global ROM index dynamically based on files on disk
  core::GlobalRom gr;
  gr.size = file.size;
  gr.crc32 = file.crc32;
  gr.md5 = file.md5;
  gr.sha1 = file.sha1;
  gr.sha256 = file.sha256;
  auto g_res = upsert_global_rom(gr);
  if (!g_res) {
    return std::unexpected(g_res.error());
  }

  // 2. Upsert the physical file record, linking to the global identity
  auto stmt =
      prepare("INSERT INTO files "
              "(path, archive_path, entry_name, size, crc32, md5, sha1, sha256, "
              "last_scanned, last_write_time) "
              "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, strftime('%s','now'), ?9) "
              "ON CONFLICT(path) DO UPDATE SET "
              "archive_path = excluded.archive_path, entry_name = excluded.entry_name, "
              "size = excluded.size, crc32 = excluded.crc32, "
              "md5 = excluded.md5, sha1 = excluded.sha1, sha256 = excluded.sha256, "
              "last_scanned = strftime('%s','now'), last_write_time = excluded.last_write_time");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_text(1, file.path);
  if (file.archive_path.has_value()) {
    stmt->bind_text(2, *file.archive_path);
  } else {
    stmt->bind_null(2);
  }
  if (file.entry_name.has_value()) {
    stmt->bind_text(3, *file.entry_name);
  } else {
    stmt->bind_null(3);
  }
  stmt->bind_int64(4, file.size);
  stmt->bind_blob(5, hex_to_bytes(file.crc32));
  stmt->bind_blob(6, hex_to_bytes(file.md5));
  stmt->bind_blob(7, hex_to_bytes(file.sha1));
  if (file.sha256.empty()) {
    stmt->bind_null(8);
  } else {
    stmt->bind_blob(8, hex_to_bytes(file.sha256));
  }
  stmt->bind_int64(9, file.last_write_time);
  stmt->execute();

  // Get the id (either inserted or updated)
  auto find = prepare("SELECT id FROM files WHERE path = ?1");
  if (!find) {
    return std::unexpected(find.error());
  }
  find->bind_text(1, file.path);
  if (find->step()) {
    return find->column_int64(0);
  }
  return last_insert_id();
}

auto Database::find_file_by_path(std::string_view path) -> Result<std::optional<core::FileInfo>> {
  auto stmt =
      prepare("SELECT id, path, archive_path, entry_name, size, crc32, md5, sha1, sha256, "
              "last_scanned, last_write_time FROM files WHERE path = ?1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_text(1, path);
  if (!stmt->step()) {
    return std::nullopt;
  }

  return core::FileInfo{
      .id = stmt->column_int64(0),
      .path = stmt->column_text(1),
      .archive_path = stmt->column_optional_text(2),
      .entry_name = stmt->column_optional_text(3),
      .size = stmt->column_int64(4),
      .crc32 = bytes_to_hex(stmt->column_blob(5)),
      .md5 = bytes_to_hex(stmt->column_blob(6)),
      .sha1 = bytes_to_hex(stmt->column_blob(7)),
      .sha256 = bytes_to_hex(stmt->column_blob(8)),
      .last_scanned = stmt->column_int64(9),
      .last_write_time = stmt->column_int64(10),
  };
}

auto Database::get_all_files() -> Result<std::vector<core::FileInfo>> {
  auto stmt = prepare("SELECT id, path, archive_path, entry_name, size, crc32, md5, sha1, sha256, "
                      "last_scanned, last_write_time FROM files");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  std::vector<core::FileInfo> files;
  while (stmt->step()) {
    files.push_back({
        .id = stmt->column_int64(0),
        .path = stmt->column_text(1),
        .archive_path = stmt->column_optional_text(2),
        .entry_name = stmt->column_optional_text(3),
        .size = stmt->column_int64(4),
        .crc32 = bytes_to_hex(stmt->column_blob(5)),
        .md5 = bytes_to_hex(stmt->column_blob(6)),
        .sha1 = bytes_to_hex(stmt->column_blob(7)),
        .sha256 = bytes_to_hex(stmt->column_blob(8)),
        .last_scanned = stmt->column_int64(9),
        .last_write_time = stmt->column_int64(10),
    });
  }
  return files;
}

auto Database::get_file_fingerprints() -> Result<core::FingerprintMap> {
  auto stmt = prepare("SELECT path, size, last_write_time FROM files");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }
  core::FingerprintMap fingerprints;
  while (stmt->step()) {
    fingerprints.emplace(stmt->column_text(0),
                         core::FileFingerprint{
                             .size = stmt->column_int64(1),
                             .last_write_time = stmt->column_int64(2),
                         });
  }
  return fingerprints;
}

auto Database::remove_missing_files(const std::vector<std::string>& existing_paths)
    -> Result<std::int64_t> {
  // Get all file paths from DB, remove those not in existing_paths
  auto all_files = get_all_files();
  if (!all_files) {
    return std::unexpected(all_files.error());
  }

  auto del_stmt = prepare("DELETE FROM files WHERE id = ?1");
  if (!del_stmt) {
    return std::unexpected(del_stmt.error());
  }

  std::int64_t removed = 0;
  for (const auto& file : *all_files) {
    bool found = false;
    for (const auto& path : existing_paths) {
      if (file.path == path) {
        found = true;
        break;
      }
    }
    if (!found) {
      del_stmt->bind_int64(1, file.id);
      del_stmt->execute();
      del_stmt->reset();
      ++removed;
    }
  }
  return removed;
}

// ═══════════════════════════════════════════════════════════════
// Global ROMs CRUD
// ═══════════════════════════════════════════════════════════════

auto Database::upsert_global_rom(const core::GlobalRom& rom) -> Result<void> {
  auto stmt = prepare("INSERT INTO global_roms (sha256, sha1, md5, crc32, size) "
                      "VALUES (?1, ?2, ?3, ?4, ?5) "
                      "ON CONFLICT(sha1) DO UPDATE SET "
                      "sha256 = COALESCE(NULLIF(excluded.sha256, ''), global_roms.sha256), "
                      "md5 = COALESCE(NULLIF(excluded.md5, ''), global_roms.md5), "
                      "crc32 = COALESCE(NULLIF(excluded.crc32, ''), global_roms.crc32)");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_blob(1, hex_to_bytes(rom.sha256));
  stmt->bind_blob(2, hex_to_bytes(rom.sha1));
  stmt->bind_blob(3, hex_to_bytes(rom.md5));
  stmt->bind_blob(4, hex_to_bytes(rom.crc32));
  stmt->bind_int64(5, rom.size);
  stmt->execute();

  return {};
}

auto Database::find_global_rom_by_sha256(std::string_view sha256)
    -> Result<std::optional<core::GlobalRom>> {
  auto stmt =
      prepare("SELECT sha256, sha1, md5, crc32, size FROM global_roms WHERE sha256 = ?1 LIMIT 1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }
  stmt->bind_blob(1, hex_to_bytes(sha256));
  if (!stmt->step()) {
    return std::nullopt;
  }
  return core::GlobalRom{.sha1 = bytes_to_hex(stmt->column_blob(1)),
                         .sha256 = bytes_to_hex(stmt->column_blob(0)),
                         .md5 = bytes_to_hex(stmt->column_blob(2)),
                         .crc32 = bytes_to_hex(stmt->column_blob(3)),
                         .size = stmt->column_int64(4)};
}

auto Database::find_global_rom_by_sha1(std::string_view sha1)
    -> Result<std::optional<core::GlobalRom>> {
  auto stmt =
      prepare("SELECT sha256, sha1, md5, crc32, size FROM global_roms WHERE sha1 = ?1 LIMIT 1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }
  stmt->bind_blob(1, hex_to_bytes(sha1));
  if (!stmt->step()) {
    return std::nullopt;
  }
  return core::GlobalRom{.sha1 = bytes_to_hex(stmt->column_blob(1)),
                         .sha256 = bytes_to_hex(stmt->column_blob(0)),
                         .md5 = bytes_to_hex(stmt->column_blob(2)),
                         .crc32 = bytes_to_hex(stmt->column_blob(3)),
                         .size = stmt->column_int64(4)};
}

auto Database::find_global_rom_by_md5(std::string_view md5)
    -> Result<std::optional<core::GlobalRom>> {
  auto stmt =
      prepare("SELECT sha256, sha1, md5, crc32, size FROM global_roms WHERE md5 = ?1 LIMIT 1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }
  stmt->bind_blob(1, hex_to_bytes(md5));
  if (!stmt->step()) {
    return std::nullopt;
  }
  return core::GlobalRom{.sha1 = bytes_to_hex(stmt->column_blob(1)),
                         .sha256 = bytes_to_hex(stmt->column_blob(0)),
                         .md5 = bytes_to_hex(stmt->column_blob(2)),
                         .crc32 = bytes_to_hex(stmt->column_blob(3)),
                         .size = stmt->column_int64(4)};
}

auto Database::find_global_rom_by_crc32(std::string_view crc32)
    -> Result<std::vector<core::GlobalRom>> {
  auto stmt = prepare("SELECT sha256, sha1, md5, crc32, size FROM global_roms WHERE crc32 = ?1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }
  stmt->bind_blob(1, hex_to_bytes(crc32));
  std::vector<core::GlobalRom> res;
  while (stmt->step()) {
    res.push_back(core::GlobalRom{.sha1 = bytes_to_hex(stmt->column_blob(1)),
                                  .sha256 = bytes_to_hex(stmt->column_blob(0)),
                                  .md5 = bytes_to_hex(stmt->column_blob(2)),
                                  .crc32 = bytes_to_hex(stmt->column_blob(3)),
                                  .size = stmt->column_int64(4)});
  }
  return res;
}

auto Database::get_all_global_roms() -> Result<std::vector<core::GlobalRom>> {
  auto stmt = prepare("SELECT sha256, sha1, md5, crc32, size FROM global_roms");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  std::vector<core::GlobalRom> global_roms;
  while (stmt->step()) {
    global_roms.push_back(core::GlobalRom{.sha1 = bytes_to_hex(stmt->column_blob(1)),
                                          .sha256 = bytes_to_hex(stmt->column_blob(0)),
                                          .md5 = bytes_to_hex(stmt->column_blob(2)),
                                          .crc32 = bytes_to_hex(stmt->column_blob(3)),
                                          .size = stmt->column_int64(4)});
  }
  return global_roms;
}

auto Database::has_files_for_global_rom(std::string_view global_rom_sha1) -> Result<bool> {
  auto stmt = prepare("SELECT 1 FROM files WHERE sha1 = ?1 LIMIT 1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }
  stmt->bind_blob(1, hex_to_bytes(global_rom_sha1));
  return stmt->step();
}

// ═══════════════════════════════════════════════════════════════
// ROM Matches CRUD
// ═══════════════════════════════════════════════════════════════

auto Database::insert_rom_match(const core::MatchResult& match) -> Result<void> {
  auto stmt = prepare("INSERT OR REPLACE INTO rom_matches (rom_id, global_rom_sha1, match_type) "
                      "VALUES (?1, ?2, ?3)");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_int64(1, match.rom_id);
  stmt->bind_blob(2, hex_to_bytes(match.global_rom_sha1));
  stmt->bind_int64(3, match_type_to_int(match.match_type));
  stmt->execute();

  return {};
}

auto Database::get_matches_for_rom(std::int64_t rom_id) -> Result<std::vector<core::MatchResult>> {
  auto stmt =
      prepare("SELECT rom_id, global_rom_sha1, match_type FROM rom_matches WHERE rom_id = ?1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_int64(1, rom_id);

  std::vector<core::MatchResult> matches;
  while (stmt->step()) {
    matches.push_back({
        .rom_id = stmt->column_int64(0),
        .global_rom_sha1 = bytes_to_hex(stmt->column_blob(1)),
        .match_type = int_to_match_type(static_cast<int>(stmt->column_int64(2))),
    });
  }
  return matches;
}

auto Database::clear_matches() -> Result<void> {
  return execute("DELETE FROM rom_matches");
}

// ═══════════════════════════════════════════════════════════════
// Computed Status (from rom_matches + files — no separate status table)
// ═══════════════════════════════════════════════════════════════

auto Database::get_computed_rom_status(std::int64_t rom_id) -> Result<core::RomStatusType> {
  // Query all matches for this ROM, left-joining to files to detect which ones
  // have a physical file on disk.
  auto stmt = prepare(
      "SELECT rm.match_type, (f.sha1 IS NOT NULL) AS has_file "
      "FROM rom_matches rm "
      "LEFT JOIN files f ON rm.global_rom_sha1 = f.sha1 "
      "WHERE rm.rom_id = ?1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_int64(1, rom_id);

  bool has_any_match = false;
  bool has_exact = false;
  bool has_partial = false;

  while (stmt->step()) {
    has_any_match = true;
    const bool file_exists = stmt->column_int64(1) != 0;
    if (file_exists) {
      const auto mt = int_to_match_type(static_cast<int>(stmt->column_int64(0)));
      if (mt == core::MatchType::Exact) {
        has_exact = true;
      } else if (mt != core::MatchType::NoMatch) {
        has_partial = true;
      }
    }
  }

  if (!has_any_match) {
    return core::RomStatusType::Missing;
  }
  if (has_exact) {
    return core::RomStatusType::Verified;
  }
  if (has_partial) {
    return core::RomStatusType::Unverified;
  }
  return core::RomStatusType::Mismatch;
}

auto Database::get_collection_summary(std::optional<std::int64_t> dat_version_id)
    -> Result<core::CollectionSummary> {
  // Compute status dynamically using a CTE.
  // Status mapping: 0=Verified, 1=Missing, 2=Unverified, 3=Mismatch
  std::string sql =
      "WITH computed AS ("
      "  SELECT r.id AS rom_id,"
      "    CASE"
      "      WHEN COUNT(rm.global_rom_sha1) = 0 THEN 1"  // 1=Missing: no match entry
      "      WHEN MAX(CASE WHEN rm.match_type = 0 AND f.sha1 IS NOT NULL THEN 1 ELSE 0 END) = 1"
      "           THEN 0"  // 0=Verified: exact match (type=0) with file on disk
      "      WHEN MAX(CASE WHEN f.sha1 IS NOT NULL THEN 1 ELSE 0 END) = 1 THEN 2"  // 2=Unverified: partial match + file present
      "      ELSE 3"  // 3=Mismatch: match exists but file deleted
      "    END AS status"
      "  FROM roms r"
      "  JOIN games g ON r.game_id = g.id"
      "  LEFT JOIN rom_matches rm ON r.id = rm.rom_id"
      "  LEFT JOIN files f ON rm.global_rom_sha1 = f.sha1";

  if (dat_version_id.has_value()) {
    sql += "  WHERE g.dat_version_id = ?1";
  }

  sql +=
      "  GROUP BY r.id"
      ")"
      "SELECT"
      "  COALESCE((SELECT name FROM dat_versions WHERE id = ?1), 'All DATs'),"
      "  COUNT(*),"
      "  SUM(CASE WHEN status = 0 THEN 1 ELSE 0 END),"  // 0=Verified
      "  SUM(CASE WHEN status = 1 THEN 1 ELSE 0 END),"  // 1=Missing
      "  SUM(CASE WHEN status = 2 THEN 1 ELSE 0 END),"  // 2=Unverified
      "  SUM(CASE WHEN status = 3 THEN 1 ELSE 0 END)"   // 3=Mismatch
      " FROM computed";

  auto stmt = prepare(sql);
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  if (dat_version_id.has_value()) {
    stmt->bind_int64(1, *dat_version_id);
  } else {
    stmt->bind_null(1);
  }

  core::CollectionSummary summary;
  if (stmt->step()) {
    summary.dat_name = stmt->column_text(0);
    summary.total_roms = stmt->column_int64(1);
    summary.verified = stmt->column_int64(2);
    summary.missing = stmt->column_int64(3);
    summary.unverified = stmt->column_int64(4);
    summary.mismatch = stmt->column_int64(5);
  }
  return summary;
}

auto Database::get_missing_roms(std::optional<std::int64_t> dat_version_id)
    -> Result<std::vector<core::MissingRom>> {
  // A ROM is "missing" if it has no matching file on disk.
  std::string sql =
      "SELECT g.name, r.name, dv.name, "
      "  COALESCE(lower(hex(r.expected_sha1)), '') "
      "FROM roms r "
      "JOIN games g ON r.game_id = g.id "
      "JOIN dat_versions dv ON g.dat_version_id = dv.id "
      "WHERE NOT EXISTS ("
      "  SELECT 1 FROM rom_matches rm "
      "  JOIN files f ON rm.global_rom_sha1 = f.sha1 "
      "  WHERE rm.rom_id = r.id"
      ")";

  if (dat_version_id.has_value()) {
    sql += " AND g.dat_version_id = ?1";
  }
  sql += " ORDER BY dv.name, g.name, r.name";

  auto stmt = prepare(sql);
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  if (dat_version_id.has_value()) {
    stmt->bind_int64(1, *dat_version_id);
  }

  std::vector<core::MissingRom> missing;
  while (stmt->step()) {
    missing.push_back({
        .game_name = stmt->column_text(0),
        .rom_name = stmt->column_text(1),
        .dat_name = stmt->column_text(2),
        .sha1 = stmt->column_text(3),
    });
  }
  return missing;
}

auto Database::get_duplicate_files(std::optional<std::int64_t> dat_version_id)
    -> Result<std::vector<core::DuplicateFile>> {
  // Find files that share the same global ROM identity (sha1) with at least one other file.
  std::string sql =
      "SELECT f.path, r.name, g.name "
      "FROM files f "
      "JOIN global_roms gr ON f.sha1 = gr.sha1 "
      "JOIN rom_matches rm ON rm.global_rom_sha1 = gr.sha1 "
      "JOIN roms r ON rm.rom_id = r.id "
      "JOIN games g ON r.game_id = g.id "
      "WHERE f.sha1 IN ("
      "  SELECT sha1 FROM files GROUP BY sha1 HAVING COUNT(*) > 1"
      ")";

  if (dat_version_id.has_value()) {
    sql += " AND g.dat_version_id = ?1";
  }
  sql += " ORDER BY r.name, f.path";

  auto stmt = prepare(sql);
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  if (dat_version_id.has_value()) {
    stmt->bind_int64(1, *dat_version_id);
  }

  std::vector<core::DuplicateFile> dupes;
  while (stmt->step()) {
    dupes.push_back({
        .file_path = stmt->column_text(0),
        .rom_name = stmt->column_text(1),
        .game_name = stmt->column_text(2),
    });
  }
  return dupes;
}

auto Database::get_unverified_files() -> Result<std::vector<core::FileInfo>> {
  // Use LEFT JOIN to find files with no matches in a single query.
  const std::string_view sql =
      "SELECT f.id, f.path, f.archive_path, f.entry_name, f.size, f.crc32, f.md5, f.sha1, "
      "f.sha256, f.last_scanned "
      "FROM files f "
      "LEFT JOIN rom_matches rm ON f.sha1 = rm.global_rom_sha1 "
      "WHERE rm.global_rom_sha1 IS NULL "
      "ORDER BY f.path";

  auto stmt = prepare(sql);
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  std::vector<core::FileInfo> unverified;
  while (stmt->step()) {
    unverified.push_back({
        .id = stmt->column_int64(0),
        .path = stmt->column_text(1),
        .archive_path = stmt->column_optional_text(2),
        .entry_name = stmt->column_optional_text(3),
        .size = stmt->column_int64(4),
        .crc32 = bytes_to_hex(stmt->column_blob(5)),
        .md5 = bytes_to_hex(stmt->column_blob(6)),
        .sha1 = bytes_to_hex(stmt->column_blob(7)),
        .sha256 = bytes_to_hex(stmt->column_blob(8)),
        .last_scanned = stmt->column_int64(9),
    });
  }
  return unverified;
}

// ═══════════════════════════════════════════════════════════════
// Scanned Directories
// ═══════════════════════════════════════════════════════════════

auto Database::add_scanned_directory(std::string_view path) -> Result<core::ScannedDirectory> {
  // ON CONFLICT DO UPDATE with a no-op assignment is intentional: it ensures
  // the RETURNING clause fires even when the path already exists, so we always
  // get the stored row back.  DO NOTHING would silence RETURNING on conflict.
  auto stmt = prepare("INSERT INTO scanned_directories (path) VALUES (?1) "
                      "ON CONFLICT(path) DO UPDATE SET path = excluded.path "
                      "RETURNING id, path, added_at");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }
  stmt->bind_text(1, path);
  if (!stmt->step()) {
    return std::unexpected(
        core::Error{core::ErrorCode::DatabaseQueryError, "Failed to insert scanned directory"});
  }
  return core::ScannedDirectory{
      .id = stmt->column_int64(0),
      .path = stmt->column_text(1),
      .added_at = stmt->column_text(2),
      .file_count = 0, // newly registered directory has no scanned files yet
  };
}

auto Database::get_all_scanned_directories() -> Result<std::vector<core::ScannedDirectory>> {
  // Count files whose virtual path starts with the directory path.
  // RTRIM normalizes any trailing '/' or '\' on the stored path before comparison so
  // directories registered with or without a trailing separator both work correctly.
  // COLLATE BINARY overrides the NOCASE collation on files.path to ensure case-sensitive
  // prefix matching (important on Linux where paths are case-sensitive).
  // Two path separators are checked to cover both Unix ('/') and Windows ('\') paths.
  auto stmt = prepare(
      "SELECT sd.id, sd.path, sd.added_at, "
      "  (SELECT COUNT(*) FROM files f "
      "   WHERE SUBSTR(f.path, 1, LENGTH(RTRIM(sd.path, '/\\')) + 1) "
      "         = (RTRIM(sd.path, '/\\') || '/') COLLATE BINARY "
      "      OR SUBSTR(f.path, 1, LENGTH(RTRIM(sd.path, '/\\')) + 1) "
      "         = (RTRIM(sd.path, '/\\') || '\\') COLLATE BINARY) AS file_count "
      "FROM scanned_directories sd "
      "ORDER BY sd.added_at");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }
  std::vector<core::ScannedDirectory> dirs;
  while (stmt->step()) {
    dirs.push_back({
        .id = stmt->column_int64(0),
        .path = stmt->column_text(1),
        .added_at = stmt->column_text(2),
        .file_count = stmt->column_int64(3),
    });
  }
  return dirs;
}

auto Database::remove_scanned_directory(std::int64_t id) -> Result<void> {
  auto stmt = prepare("DELETE FROM scanned_directories WHERE id = ?1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }
  stmt->bind_int64(1, id);
  stmt->execute();

  if (sqlite3_changes(db_) == 0) {
    return std::unexpected(
        core::Error{core::ErrorCode::DatabaseQueryError, "Scanned directory not found"});
  }
  return {};
}

// ═══════════════════════════════════════════════════════════════
// DB Explorer
// ═══════════════════════════════════════════════════════════════

auto Database::get_table_names() -> Result<std::vector<std::string>> {
  auto stmt = prepare("SELECT name FROM sqlite_master "
                      "WHERE type='table' AND name NOT LIKE 'sqlite_%' "
                      "ORDER BY name");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }
  std::vector<std::string> names;
  while (stmt->step()) {
    names.push_back(stmt->column_text(0));
  }
  return names;
}

auto Database::query_table_data(std::string_view table_name) -> Result<core::TableQueryResult> {
  // Validate table_name against known tables (defence against SQL injection).
  auto known_tables = get_table_names();
  if (!known_tables) {
    return std::unexpected(known_tables.error());
  }
  const bool is_known = std::ranges::any_of(*known_tables, [table_name](const std::string& n) {
    return n == table_name;
  });
  if (!is_known) {
    return std::unexpected(
        core::Error{core::ErrorCode::DatabaseQueryError,
                    "Table '" + std::string(table_name) + "' does not exist"});
  }

  const std::string tname{table_name}; // validated above

  // ── Step 1: Column metadata from PRAGMA table_info ──────────────
  // Columns: cid | name | type | notnull | dflt_value | pk
  //          0     1      2      3         4             5
  // pk = 0 means not a PK; pk > 0 is the 1-based position within a compound PK.
  auto pragma_stmt = prepare("PRAGMA table_info(" + quote_identifier(tname) + ")");
  if (!pragma_stmt) {
    return std::unexpected(pragma_stmt.error());
  }

  core::TableQueryResult result;
  while (pragma_stmt->step()) {
    core::ColumnInfo col;
    col.name           = pragma_stmt->column_text(1);
    col.type           = pragma_stmt->column_text(2);
    col.not_null       = pragma_stmt->column_int64(3) != 0;
    const auto pk_ord  = pragma_stmt->column_int64(5);
    col.pk_order       = static_cast<int>(pk_ord);
    col.is_primary_key = pk_ord > 0;
    result.columns.push_back(std::move(col));
  }

  if (result.columns.empty()) {
    return std::unexpected(
        core::Error{core::ErrorCode::DatabaseQueryError,
                    "Table '" + tname + "' has no columns"});
  }

  // Helper: locate a column by name (linear search — tables have few columns).
  auto find_col = [&result](const std::string& name) -> std::optional<std::size_t> {
    for (std::size_t i = 0; i < result.columns.size(); ++i) {
      if (result.columns[i].name == name) {
        return i;
      }
    }
    return std::nullopt;
  };

  // ── Step 2: Unique columns from PRAGMA index_list ────────────────
  // Columns: seq | name | unique | origin | partial
  //          0     1      2        3        4
  // origin = 'pk' for the implicit PK index; 'u' for UNIQUE constraints; 'c' for CREATE INDEX.
  {
    auto idx_list = prepare("PRAGMA index_list(" + quote_identifier(tname) + ")");
    if (idx_list) {
      while (idx_list->step()) {
        const bool is_unique  = idx_list->column_int64(2) != 0;
        const std::string origin = idx_list->column_text(3);
        // Skip non-unique or pk-origin indexes (pk columns already flagged in Step 1).
        if (!is_unique || origin == "pk") {
          continue;
        }
        const std::string idx_name = idx_list->column_text(1);
        auto idx_info = prepare("PRAGMA index_info(" + quote_identifier(idx_name) + ")");
        if (!idx_info) {
          continue;
        }
        // Columns: seqno | cid | name
        while (idx_info->step()) {
          if (auto pos = find_col(idx_info->column_text(2))) {
            result.columns[*pos].is_unique = true;
          }
        }
      }
    }
  }

  // ── Step 3: Foreign key columns from PRAGMA foreign_key_list ────
  // Columns: id | seq | table | from | to | on_update | on_delete | match
  //          0    1     2       3      4    5            6           7
  {
    auto fk_list = prepare("PRAGMA foreign_key_list(" + quote_identifier(tname) + ")");
    if (fk_list) {
      while (fk_list->step()) {
        const std::string fk_table = fk_list->column_text(2);
        const std::string from_col = fk_list->column_text(3);
        const std::string to_col   = fk_list->column_text(4);
        if (auto pos = find_col(from_col)) {
          result.columns[*pos].fk_table  = fk_table;
          result.columns[*pos].fk_column = to_col;
        }
      }
    }
  }

  // ── Step 4: All rows — no artificial row limit ───────────────────
  // BLOBs are converted to lowercase hex by column_display_text().
  // INTEGER columns that store Unix epoch seconds are wrapped in
  // datetime(..., 'unixepoch', 'localtime') so they render as
  // "YYYY-MM-DD HH:MM:SS" in the DB browser, matching the TEXT
  // timestamp columns (imported_at, added_at).
  static const std::unordered_set<std::string> k_EpochColumns{"last_scanned"};
  std::string col_list;
  for (std::size_t i = 0; i < result.columns.size(); ++i) {
    if (i > 0) {
      col_list += ", ";
    }
    const auto& cname = result.columns[i].name;
    if (k_EpochColumns.contains(cname)) {
      col_list += "datetime(" + quote_identifier(cname) +
                  ", 'unixepoch', 'localtime') AS " + quote_identifier(cname);
    } else {
      col_list += quote_identifier(cname);
    }
  }
  auto select_stmt = prepare("SELECT " + col_list + " FROM " + quote_identifier(tname));
  if (!select_stmt) {
    return std::unexpected(select_stmt.error());
  }

  const auto col_count = static_cast<int>(result.columns.size());
  while (select_stmt->step()) {
    std::vector<std::string> row;
    row.reserve(static_cast<std::size_t>(col_count));
    for (int i = 0; i < col_count; ++i) {
      row.push_back(select_stmt->column_display_text(i));
    }
    result.rows.push_back(std::move(row));
  }
  return result;
}

} // namespace romulus::database
