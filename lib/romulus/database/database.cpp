#include "romulus/database/database.hpp"

#include "romulus/core/logging.hpp"

#include <sqlite3.h>

#include <stdexcept>
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

// ═══════════════════════════════════════════════════════════════
// TransactionGuard
// ═══════════════════════════════════════════════════════════════

TransactionGuard::TransactionGuard(sqlite3* db) : db_(db) {
  sqlite3_exec(db_, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
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

void TransactionGuard::commit() {
  if (db_ != nullptr && !committed_) {
    sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
    committed_ = true;
  }
}

void TransactionGuard::rollback() {
  if (db_ != nullptr && !committed_) {
    sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    committed_ = true; // Prevent double-rollback in destructor
  }
}

// ═══════════════════════════════════════════════════════════════
// Database — Construction & Configuration
// ═══════════════════════════════════════════════════════════════

namespace {

constexpr std::string_view k_Schema = R"SQL(
CREATE TABLE IF NOT EXISTS systems (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL UNIQUE,
    short_name  TEXT,
    extensions  TEXT
);

CREATE TABLE IF NOT EXISTS dat_versions (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    system_id     INTEGER NOT NULL REFERENCES systems(id),
    name          TEXT NOT NULL,
    version       TEXT NOT NULL,
    source_url    TEXT,
    checksum      TEXT NOT NULL,
    imported_at   TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(name, version)
);

CREATE TABLE IF NOT EXISTS games (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    name            TEXT NOT NULL,
    system_id       INTEGER NOT NULL REFERENCES systems(id),
    dat_version_id  INTEGER NOT NULL REFERENCES dat_versions(id),
    UNIQUE(name, system_id, dat_version_id)
);

CREATE TABLE IF NOT EXISTS roms (
    id       INTEGER PRIMARY KEY AUTOINCREMENT,
    game_id  INTEGER NOT NULL REFERENCES games(id),
    name     TEXT NOT NULL,
    size     INTEGER,
    crc32    TEXT,
    md5      TEXT,
    sha1     TEXT,
    sha256   TEXT,
    region   TEXT
);

CREATE TABLE IF NOT EXISTS global_roms (
    sha1          BLOB PRIMARY KEY,
    sha256        BLOB,
    md5           BLOB,
    crc32         BLOB,
    size          INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS files (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    filename      TEXT NOT NULL,
    path          TEXT NOT NULL UNIQUE,
    size          INTEGER NOT NULL,
    crc32         BLOB,
    md5           BLOB,
    sha1          BLOB REFERENCES global_roms(sha1),
    sha256        BLOB NOT NULL,
    last_scanned  TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS rom_matches (
    rom_id        INTEGER NOT NULL REFERENCES roms(id),
    global_rom_sha1 BLOB NOT NULL REFERENCES global_roms(sha1),
    match_type    TEXT NOT NULL,
    PRIMARY KEY (rom_id, global_rom_sha1)
);

CREATE TABLE IF NOT EXISTS rom_status (
    rom_id        INTEGER PRIMARY KEY REFERENCES roms(id),
    status        TEXT NOT NULL,
    last_updated  TEXT NOT NULL DEFAULT (datetime('now'))
);

-- Persists ROM scan directory paths across application sessions.
-- Users register directories via the Folders tab; they are rescanned on demand.
CREATE TABLE IF NOT EXISTS scanned_directories (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    path      TEXT NOT NULL UNIQUE,
    added_at  TEXT NOT NULL DEFAULT (datetime('now'))
);

-- Indexes for fast hash lookups
CREATE INDEX IF NOT EXISTS idx_roms_sha1 ON roms(sha1);
CREATE INDEX IF NOT EXISTS idx_roms_md5 ON roms(md5);
CREATE INDEX IF NOT EXISTS idx_roms_crc32 ON roms(crc32);
CREATE UNIQUE INDEX IF NOT EXISTS idx_roms_sha256 ON roms(sha256) WHERE sha256 IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_files_sha1 ON files(sha1);
CREATE INDEX IF NOT EXISTS idx_files_crc32 ON files(crc32);
CREATE INDEX IF NOT EXISTS idx_files_sha256 ON files(sha256);
CREATE INDEX IF NOT EXISTS idx_global_roms_sha1 ON global_roms(sha1);
CREATE INDEX IF NOT EXISTS idx_global_roms_md5 ON global_roms(md5) WHERE md5 IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_rom_status_status ON rom_status(status);
)SQL";

auto match_type_to_string(core::MatchType type) -> std::string_view {
  switch (type) {
    case core::MatchType::Exact:
      return "exact";
    case core::MatchType::Sha256Only:
      return "sha256_only";
    case core::MatchType::Sha1Only:
      return "sha1_only";
    case core::MatchType::Md5Only:
      return "md5_only";
    case core::MatchType::Crc32Only:
      return "crc32_only";
    case core::MatchType::SizeOnly:
      return "size_only";
    case core::MatchType::NoMatch:
      return "no_match";
  }
  return "unknown";
}

auto string_to_match_type(std::string_view str) -> core::MatchType {
  if (str == "exact") {
    return core::MatchType::Exact;
  }
  if (str == "sha256_only") {
    return core::MatchType::Sha256Only;
  }
  if (str == "sha1_only") {
    return core::MatchType::Sha1Only;
  }
  if (str == "md5_only") {
    return core::MatchType::Md5Only;
  }
  if (str == "crc32_only") {
    return core::MatchType::Crc32Only;
  }
  if (str == "size_only") {
    return core::MatchType::SizeOnly;
  }
  return core::MatchType::NoMatch;
}

auto status_to_string(core::RomStatusType status) -> std::string_view {
  switch (status) {
    case core::RomStatusType::Verified:
      return "verified";
    case core::RomStatusType::Missing:
      return "missing";
    case core::RomStatusType::Unverified:
      return "unverified";
    case core::RomStatusType::Mismatch:
      return "mismatch";
  }
  return "unknown";
}

auto string_to_status(std::string_view str) -> core::RomStatusType {
  if (str == "verified") {
    return core::RomStatusType::Verified;
  }
  if (str == "missing") {
    return core::RomStatusType::Missing;
  }
  if (str == "unverified") {
    return core::RomStatusType::Unverified;
  }
  if (str == "mismatch") {
    return core::RomStatusType::Mismatch;
  }
  return core::RomStatusType::Missing;
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
  // Upgrade existing databases: add new columns if they do not exist yet.
  // These must run BEFORE k_Schema so that the CREATE INDEX statements referencing
  // sha256/filename do not fail with "no such column" on pre-existing databases.
  // Only "duplicate column name" errors are suppressed; all others emit a warning.
  auto try_alter = [this](const char* sql) {
    char* msg = nullptr;
    int alter_rc = sqlite3_exec(db_, sql, nullptr, nullptr, &msg);
    if (alter_rc != SQLITE_OK && msg != nullptr) {
      std::string err(msg);
      sqlite3_free(msg);
      // SQLite reports "duplicate column name" when a column already exists; treat as no-op.
      // "no such table" fires on fresh databases before k_Schema creates them; also no-op.
      if (err.find("duplicate column name") != std::string::npos ||
          err.find("no such table") != std::string::npos) {
        return;
      }
      // Duplicate SHA256 values already in DB will block creating the unique index.
      // SQLite uses "UNIQUE constraint failed" for both DML and index-creation failures.
      // We check for the keywords broadly to cover all formulations.
      if (err.find("UNIQUE constraint") != std::string::npos ||
          err.find("unique constraint") != std::string::npos) {
        ROMULUS_WARN("Schema upgrade warning: could not create unique index on roms.sha256 "
                     "because duplicate SHA256 values already exist. "
                     "SHA256 uniqueness will not be enforced until duplicates are resolved.");
        return;
      }
      ROMULUS_WARN("Schema upgrade warning: {}", err);
    }
  };

  try_alter("ALTER TABLE roms ADD COLUMN sha256 TEXT");
  try_alter("ALTER TABLE files ADD COLUMN sha256 TEXT NOT NULL DEFAULT ''");
  try_alter("ALTER TABLE files ADD COLUMN filename TEXT NOT NULL DEFAULT ''");
  // Enforce uniqueness for upgraded databases (new DBs get it via k_Schema).
  try_alter(
      "CREATE UNIQUE INDEX IF NOT EXISTS idx_roms_sha256 ON roms(sha256) WHERE sha256 IS NOT NULL");

  // Apply the main schema (CREATE TABLE/INDEX IF NOT EXISTS — safe to run on any DB state).
  char* err_msg = nullptr;
  int rc = sqlite3_exec(db_, std::string(k_Schema).c_str(), nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    std::string err = err_msg != nullptr ? err_msg : "unknown error";
    sqlite3_free(err_msg);
    throw std::runtime_error("Migration failed: " + err);
  }

  ROMULUS_DEBUG("Database migrations applied successfully");
}

auto Database::begin_transaction() -> TransactionGuard {
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
// Systems CRUD
// ═══════════════════════════════════════════════════════════════

auto Database::insert_system(const core::SystemInfo& system) -> Result<std::int64_t> {
  auto stmt = prepare("INSERT INTO systems (name, short_name, extensions) VALUES (?1, ?2, ?3)");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_text(1, system.name);
  stmt->bind_text(2, system.short_name);
  stmt->bind_text(3, system.extensions);
  stmt->execute();

  return last_insert_id();
}

auto Database::find_system_by_name(std::string_view name)
    -> Result<std::optional<core::SystemInfo>> {
  auto stmt = prepare("SELECT id, name, short_name, extensions FROM systems WHERE name = ?1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_text(1, name);
  if (!stmt->step()) {
    return std::nullopt;
  }

  return core::SystemInfo{
      .id = stmt->column_int64(0),
      .name = stmt->column_text(1),
      .short_name = stmt->column_text(2),
      .extensions = stmt->column_text(3),
  };
}

auto Database::get_all_systems() -> Result<std::vector<core::SystemInfo>> {
  auto stmt = prepare("SELECT id, name, short_name, extensions FROM systems ORDER BY name");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  std::vector<core::SystemInfo> systems;
  while (stmt->step()) {
    systems.push_back({
        .id = stmt->column_int64(0),
        .name = stmt->column_text(1),
        .short_name = stmt->column_text(2),
        .extensions = stmt->column_text(3),
    });
  }
  return systems;
}

auto Database::get_or_create_system(std::string_view name) -> Result<std::int64_t> {
  auto found = find_system_by_name(name);
  if (!found) {
    return std::unexpected(found.error());
  }

  if (found->has_value()) {
    return found->value().id;
  }

  core::SystemInfo sys{.name = std::string(name), .short_name = {}, .extensions = {}};
  return insert_system(sys);
}

// ═══════════════════════════════════════════════════════════════
// DAT Versions CRUD
// ═══════════════════════════════════════════════════════════════

auto Database::insert_dat_version(const core::DatVersion& dat) -> Result<std::int64_t> {
  auto stmt = prepare("INSERT INTO dat_versions (system_id, name, version, source_url, checksum) "
                      "VALUES (?1, ?2, ?3, ?4, ?5)");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_int64(1, dat.system_id);
  stmt->bind_text(2, dat.name);
  stmt->bind_text(3, dat.version);
  stmt->bind_text(4, dat.source_url);
  stmt->bind_text(5, dat.checksum);
  stmt->execute();

  return last_insert_id();
}

auto Database::find_dat_version(std::string_view name, std::string_view version)
    -> Result<std::optional<core::DatVersion>> {
  auto stmt = prepare("SELECT id, system_id, name, version, source_url, checksum, imported_at "
                      "FROM dat_versions WHERE name = ?1 AND version = ?2");
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
      .system_id = stmt->column_int64(1),
      .name = stmt->column_text(2),
      .version = stmt->column_text(3),
      .source_url = stmt->column_text(4),
      .checksum = stmt->column_text(5),
      .imported_at = stmt->column_text(6),
  };
}

auto Database::get_latest_dat_version(std::int64_t system_id)
    -> Result<std::optional<core::DatVersion>> {
  auto stmt = prepare("SELECT id, system_id, name, version, source_url, checksum, imported_at "
                      "FROM dat_versions WHERE system_id = ?1 ORDER BY imported_at DESC LIMIT 1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_int64(1, system_id);
  if (!stmt->step()) {
    return std::nullopt;
  }

  return core::DatVersion{
      .id = stmt->column_int64(0),
      .system_id = stmt->column_int64(1),
      .name = stmt->column_text(2),
      .version = stmt->column_text(3),
      .source_url = stmt->column_text(4),
      .checksum = stmt->column_text(5),
      .imported_at = stmt->column_text(6),
  };
}

auto Database::get_all_dat_versions() -> Result<std::vector<core::DatVersion>> {
  auto stmt = prepare("SELECT id, system_id, name, version, source_url, checksum, imported_at "
                      "FROM dat_versions ORDER BY imported_at DESC");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  std::vector<core::DatVersion> versions;
  while (stmt->step()) {
    versions.push_back({
        .id = stmt->column_int64(0),
        .system_id = stmt->column_int64(1),
        .name = stmt->column_text(2),
        .version = stmt->column_text(3),
        .source_url = stmt->column_text(4),
        .checksum = stmt->column_text(5),
        .imported_at = stmt->column_text(6),
    });
  }
  return versions;
}

auto Database::get_roms_for_dat_version(std::int64_t dat_version_id)
    -> Result<std::vector<core::RomInfo>> {
  auto stmt = prepare("SELECT r.id, r.game_id, r.name, r.size, r.crc32, r.md5, r.sha1, r.sha256, "
                      "r.region "
                      "FROM roms r JOIN games g ON r.game_id = g.id "
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
        .name = stmt->column_text(2),
        .size = stmt->column_int64(3),
        .crc32 = stmt->column_text(4),
        .md5 = stmt->column_text(5),
        .sha1 = stmt->column_text(6),
        .sha256 = stmt->column_text(7),
        .region = stmt->column_text(8),
    });
  }
  return roms;
}

// ═══════════════════════════════════════════════════════════════
// Games CRUD
// ═══════════════════════════════════════════════════════════════

auto Database::insert_game(const core::GameInfo& game) -> Result<std::int64_t> {
  auto stmt = prepare("INSERT OR IGNORE INTO games (name, system_id, dat_version_id) "
                      "VALUES (?1, ?2, ?3)");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_text(1, game.name);
  stmt->bind_int64(2, game.system_id);
  stmt->bind_int64(3, game.dat_version_id);
  stmt->execute();

  auto id = last_insert_id();
  if (id == 0) {
    // Already existed — look it up
    auto find =
        prepare("SELECT id FROM games WHERE name = ?1 AND system_id = ?2 AND dat_version_id = ?3");
    if (!find) {
      return std::unexpected(find.error());
    }
    find->bind_text(1, game.name);
    find->bind_int64(2, game.system_id);
    find->bind_int64(3, game.dat_version_id);
    if (find->step()) {
      id = find->column_int64(0);
    }
  }

  return id;
}

auto Database::get_games_by_dat_version(std::int64_t dat_version_id)
    -> Result<std::vector<core::GameInfo>> {
  auto stmt =
      prepare("SELECT id, name, system_id, dat_version_id FROM games WHERE dat_version_id = ?1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_int64(1, dat_version_id);

  std::vector<core::GameInfo> games;
  while (stmt->step()) {
    games.push_back({
        .id = stmt->column_int64(0),
        .name = stmt->column_text(1),
        .description = {},
        .system_id = stmt->column_int64(2),
        .dat_version_id = stmt->column_int64(3),
        .roms = {},
    });
  }
  return games;
}

// ═══════════════════════════════════════════════════════════════
// ROMs CRUD
// ═══════════════════════════════════════════════════════════════

auto Database::insert_rom(const core::RomInfo& rom) -> Result<std::int64_t> {
  auto stmt = prepare("INSERT INTO roms (game_id, name, size, crc32, md5, sha1, sha256, region) "
                      "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_int64(1, rom.game_id);
  stmt->bind_text(2, rom.name);
  stmt->bind_int64(3, rom.size);
  stmt->bind_text(4, rom.crc32);
  stmt->bind_text(5, rom.md5);
  stmt->bind_text(6, rom.sha1);
  if (rom.sha256.empty()) {
    stmt->bind_null(7);
  } else {
    stmt->bind_text(7, rom.sha256);
  }
  stmt->bind_text(8, rom.region);
  stmt->execute();

  return last_insert_id();
}

auto Database::find_rom_by_sha1(std::string_view sha1) -> Result<std::optional<core::RomInfo>> {
  auto stmt = prepare("SELECT id, game_id, name, size, crc32, md5, sha1, sha256, region "
                      "FROM roms WHERE sha1 = ?1 LIMIT 1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_text(1, sha1);
  if (!stmt->step()) {
    return std::nullopt;
  }

  return core::RomInfo{
      .id = stmt->column_int64(0),
      .game_id = stmt->column_int64(1),
      .name = stmt->column_text(2),
      .size = stmt->column_int64(3),
      .crc32 = stmt->column_text(4),
      .md5 = stmt->column_text(5),
      .sha1 = stmt->column_text(6),
      .sha256 = stmt->column_text(7),
      .region = stmt->column_text(8),
  };
}

auto Database::find_rom_by_sha256(std::string_view sha256) -> Result<std::optional<core::RomInfo>> {
  auto stmt = prepare("SELECT id, game_id, name, size, crc32, md5, sha1, sha256, region "
                      "FROM roms WHERE sha256 = ?1 LIMIT 1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_text(1, sha256);
  if (!stmt->step()) {
    return std::nullopt;
  }

  return core::RomInfo{
      .id = stmt->column_int64(0),
      .game_id = stmt->column_int64(1),
      .name = stmt->column_text(2),
      .size = stmt->column_int64(3),
      .crc32 = stmt->column_text(4),
      .md5 = stmt->column_text(5),
      .sha1 = stmt->column_text(6),
      .sha256 = stmt->column_text(7),
      .region = stmt->column_text(8),
  };
}

auto Database::find_rom_by_md5(std::string_view md5) -> Result<std::optional<core::RomInfo>> {
  auto stmt = prepare("SELECT id, game_id, name, size, crc32, md5, sha1, sha256, region "
                      "FROM roms WHERE md5 = ?1 LIMIT 1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_text(1, md5);
  if (!stmt->step()) {
    return std::nullopt;
  }

  return core::RomInfo{
      .id = stmt->column_int64(0),
      .game_id = stmt->column_int64(1),
      .name = stmt->column_text(2),
      .size = stmt->column_int64(3),
      .crc32 = stmt->column_text(4),
      .md5 = stmt->column_text(5),
      .sha1 = stmt->column_text(6),
      .sha256 = stmt->column_text(7),
      .region = stmt->column_text(8),
  };
}

auto Database::find_rom_by_crc32(std::string_view crc32) -> Result<std::vector<core::RomInfo>> {
  auto stmt = prepare("SELECT id, game_id, name, size, crc32, md5, sha1, sha256, region "
                      "FROM roms WHERE crc32 = ?1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_text(1, crc32);

  std::vector<core::RomInfo> roms;
  while (stmt->step()) {
    roms.push_back({
        .id = stmt->column_int64(0),
        .game_id = stmt->column_int64(1),
        .name = stmt->column_text(2),
        .size = stmt->column_int64(3),
        .crc32 = stmt->column_text(4),
        .md5 = stmt->column_text(5),
        .sha1 = stmt->column_text(6),
        .sha256 = stmt->column_text(7),
        .region = stmt->column_text(8),
    });
  }
  return roms;
}

auto Database::get_all_roms_for_system(std::int64_t system_id)
    -> Result<std::vector<core::RomInfo>> {
  auto stmt = prepare("SELECT r.id, r.game_id, r.name, r.size, r.crc32, r.md5, r.sha1, r.sha256, "
                      "r.region "
                      "FROM roms r JOIN games g ON r.game_id = g.id "
                      "WHERE g.system_id = ?1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_int64(1, system_id);

  std::vector<core::RomInfo> roms;
  while (stmt->step()) {
    roms.push_back({
        .id = stmt->column_int64(0),
        .game_id = stmt->column_int64(1),
        .name = stmt->column_text(2),
        .size = stmt->column_int64(3),
        .crc32 = stmt->column_text(4),
        .md5 = stmt->column_text(5),
        .sha1 = stmt->column_text(6),
        .sha256 = stmt->column_text(7),
        .region = stmt->column_text(8),
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
      prepare("INSERT INTO files (filename, path, size, crc32, md5, sha1, sha256, last_scanned) "
              "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, datetime('now')) "
              "ON CONFLICT(path) DO UPDATE SET "
              "filename = excluded.filename, size = excluded.size, crc32 = excluded.crc32, "
              "md5 = excluded.md5, sha1 = excluded.sha1, sha256 = excluded.sha256, "
              "last_scanned = datetime('now')");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_text(1, file.filename);
  stmt->bind_text(2, file.path);
  stmt->bind_int64(3, file.size);
  stmt->bind_blob(4, hex_to_bytes(file.crc32));
  stmt->bind_blob(5, hex_to_bytes(file.md5));
  stmt->bind_blob(6, hex_to_bytes(file.sha1));
  stmt->bind_blob(7, hex_to_bytes(file.sha256));
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
  auto stmt = prepare("SELECT id, filename, path, size, crc32, md5, sha1, sha256, last_scanned "
                      "FROM files WHERE path = ?1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_text(1, path);
  if (!stmt->step()) {
    return std::nullopt;
  }

  return core::FileInfo{
      .id = stmt->column_int64(0),
      .filename = stmt->column_text(1),
      .path = stmt->column_text(2),
      .size = stmt->column_int64(3),
      .crc32 = bytes_to_hex(stmt->column_blob(4)),
      .md5 = bytes_to_hex(stmt->column_blob(5)),
      .sha1 = bytes_to_hex(stmt->column_blob(6)),
      .sha256 = bytes_to_hex(stmt->column_blob(7)),
      .last_scanned = stmt->column_text(8),
  };
}

auto Database::get_all_files() -> Result<std::vector<core::FileInfo>> {
  auto stmt =
      prepare("SELECT id, filename, path, size, crc32, md5, sha1, sha256, last_scanned FROM files");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  std::vector<core::FileInfo> files;
  while (stmt->step()) {
    files.push_back({
        .id = stmt->column_int64(0),
        .filename = stmt->column_text(1),
        .path = stmt->column_text(2),
        .size = stmt->column_int64(3),
        .crc32 = bytes_to_hex(stmt->column_blob(4)),
        .md5 = bytes_to_hex(stmt->column_blob(5)),
        .sha1 = bytes_to_hex(stmt->column_blob(6)),
        .sha256 = bytes_to_hex(stmt->column_blob(7)),
        .last_scanned = stmt->column_text(8),
    });
  }
  return files;
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
  stmt->bind_text(3, match_type_to_string(match.match_type));
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
        .match_type = string_to_match_type(stmt->column_text(2)),
    });
  }
  return matches;
}

auto Database::clear_matches() -> Result<void> {
  return execute("DELETE FROM rom_matches");
}

// ═══════════════════════════════════════════════════════════════
// ROM Status CRUD
// ═══════════════════════════════════════════════════════════════

auto Database::upsert_rom_status(std::int64_t rom_id, core::RomStatusType status) -> Result<void> {
  auto stmt = prepare("INSERT INTO rom_status (rom_id, status, last_updated) "
                      "VALUES (?1, ?2, datetime('now')) "
                      "ON CONFLICT(rom_id) DO UPDATE SET "
                      "status = excluded.status, last_updated = datetime('now')");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_int64(1, rom_id);
  stmt->bind_text(2, status_to_string(status));
  stmt->execute();

  return {};
}

auto Database::get_rom_status(std::int64_t rom_id) -> Result<std::optional<core::RomStatusEntry>> {
  auto stmt = prepare("SELECT rom_id, status, last_updated FROM rom_status WHERE rom_id = ?1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_int64(1, rom_id);
  if (!stmt->step()) {
    return std::nullopt;
  }

  return core::RomStatusEntry{
      .rom_id = stmt->column_int64(0),
      .status = string_to_status(stmt->column_text(1)),
      .last_updated = stmt->column_text(2),
  };
}

auto Database::get_collection_summary(std::optional<std::int64_t> system_id)
    -> Result<core::CollectionSummary> {
  std::string sql;
  if (system_id.has_value()) {
    sql = "SELECT "
          "  COALESCE(s.name, 'All Systems'), "
          "  COUNT(DISTINCT r.id), "
          "  SUM(CASE WHEN rs.status = 'verified' THEN 1 ELSE 0 END), "
          "  SUM(CASE WHEN rs.status = 'missing' OR rs.status IS NULL THEN 1 ELSE 0 END), "
          "  SUM(CASE WHEN rs.status = 'unverified' THEN 1 ELSE 0 END), "
          "  SUM(CASE WHEN rs.status = 'mismatch' THEN 1 ELSE 0 END) "
          "FROM roms r "
          "JOIN games g ON r.game_id = g.id "
          "JOIN systems s ON g.system_id = s.id "
          "LEFT JOIN rom_status rs ON r.id = rs.rom_id "
          "WHERE g.system_id = ?1";
  } else {
    sql = "SELECT "
          "  'All Systems', "
          "  COUNT(DISTINCT r.id), "
          "  SUM(CASE WHEN rs.status = 'verified' THEN 1 ELSE 0 END), "
          "  SUM(CASE WHEN rs.status = 'missing' OR rs.status IS NULL THEN 1 ELSE 0 END), "
          "  SUM(CASE WHEN rs.status = 'unverified' THEN 1 ELSE 0 END), "
          "  SUM(CASE WHEN rs.status = 'mismatch' THEN 1 ELSE 0 END) "
          "FROM roms r "
          "JOIN games g ON r.game_id = g.id "
          "LEFT JOIN rom_status rs ON r.id = rs.rom_id";
  }

  auto stmt = prepare(sql);
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  if (system_id.has_value()) {
    stmt->bind_int64(1, *system_id);
  }

  core::CollectionSummary summary;
  if (stmt->step()) {
    summary.system_name = stmt->column_text(0);
    summary.total_roms = stmt->column_int64(1);
    summary.verified = stmt->column_int64(2);
    summary.missing = stmt->column_int64(3);
    summary.unverified = stmt->column_int64(4);
    summary.mismatch = stmt->column_int64(5);
  }
  return summary;
}

auto Database::get_missing_roms(std::optional<std::int64_t> system_id)
    -> Result<std::vector<core::MissingRom>> {
  std::string sql = "SELECT g.name, r.name, s.name, r.sha1 "
                    "FROM roms r "
                    "JOIN games g ON r.game_id = g.id "
                    "JOIN systems s ON g.system_id = s.id "
                    "LEFT JOIN rom_status rs ON r.id = rs.rom_id "
                    "WHERE rs.status = 'missing' OR rs.status IS NULL";

  if (system_id.has_value()) {
    sql += " AND g.system_id = ?1";
  }
  sql += " ORDER BY s.name, g.name, r.name";

  auto stmt = prepare(sql);
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  if (system_id.has_value()) {
    stmt->bind_int64(1, *system_id);
  }

  std::vector<core::MissingRom> missing;
  while (stmt->step()) {
    missing.push_back({
        .game_name = stmt->column_text(0),
        .rom_name = stmt->column_text(1),
        .system_name = stmt->column_text(2),
        .sha1 = stmt->column_text(3),
    });
  }
  return missing;
}

auto Database::get_duplicate_files(std::optional<std::int64_t> system_id)
    -> Result<std::vector<core::DuplicateFile>> {
  // Find files that share the same global ROM identity (sha1) with at least one other file.
  // A "duplicate" means multiple physical disk files resolve to the same global_rom_sha1.
  std::string sql = "SELECT f.path, r.name, g.name "
                    "FROM files f "
                    "JOIN global_roms gr ON f.sha1 = gr.sha1 "
                    "JOIN rom_matches rm ON rm.global_rom_sha1 = gr.sha1 "
                    "JOIN roms r ON rm.rom_id = r.id "
                    "JOIN games g ON r.game_id = g.id "
                    "WHERE f.sha1 IN ("
                    "  SELECT sha1 FROM files GROUP BY sha1 HAVING COUNT(*) > 1"
                    ")";

  if (system_id.has_value()) {
    sql += " AND g.system_id = ?1";
  }
  sql += " ORDER BY r.name, f.path";

  auto stmt = prepare(sql);
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  if (system_id.has_value()) {
    stmt->bind_int64(1, *system_id);
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

auto Database::get_unverified_files(std::optional<std::int64_t> /*system_id*/)
    -> Result<std::vector<core::FileInfo>> {
  // Use LEFT JOIN to find files with no matches in a single query
  std::string sql =
      "SELECT f.id, f.filename, f.path, f.size, f.crc32, f.md5, f.sha1, f.sha256, f.last_scanned "
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
        .filename = stmt->column_text(1),
        .path = stmt->column_text(2),
        .size = stmt->column_int64(3),
        .crc32 = bytes_to_hex(stmt->column_blob(4)),
        .md5 = bytes_to_hex(stmt->column_blob(5)),
        .sha1 = bytes_to_hex(stmt->column_blob(6)),
        .sha256 = bytes_to_hex(stmt->column_blob(7)),
        .last_scanned = stmt->column_text(8),
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
    return std::unexpected(Error{"Failed to insert scanned directory"});
  }
  return core::ScannedDirectory{
      .id = stmt->column_int64(0),
      .path = stmt->column_text(1),
      .added_at = stmt->column_text(2),
  };
}

auto Database::get_all_scanned_directories() -> Result<std::vector<core::ScannedDirectory>> {
  auto stmt = prepare("SELECT id, path, added_at FROM scanned_directories ORDER BY added_at");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }
  std::vector<core::ScannedDirectory> dirs;
  while (stmt->step()) {
    dirs.push_back({
        .id = stmt->column_int64(0),
        .path = stmt->column_text(1),
        .added_at = stmt->column_text(2),
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
  return {};
}

} // namespace romulus::database
