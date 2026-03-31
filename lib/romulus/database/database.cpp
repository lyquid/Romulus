#include "romulus/database/database.hpp"

#include "romulus/core/logging.hpp"

#include <sqlite3.h>

#include <stdexcept>
#include <utility>

namespace romulus::database {

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
    dat_id        TEXT,
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
    dat_game_id     TEXT,
    name            TEXT NOT NULL,
    description     TEXT,
    clone_of        TEXT,
    category        TEXT,
    game_id_text    TEXT,
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
    region   TEXT,
    status   TEXT,
    serial   TEXT,
    header   TEXT
);

CREATE TABLE IF NOT EXISTS files (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    path          TEXT NOT NULL UNIQUE,
    size          INTEGER NOT NULL,
    crc32         TEXT,
    md5           TEXT,
    sha1          TEXT,
    last_scanned  TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS file_matches (
    file_id     INTEGER NOT NULL REFERENCES files(id),
    rom_id      INTEGER NOT NULL REFERENCES roms(id),
    match_type  TEXT NOT NULL,
    PRIMARY KEY (file_id, rom_id)
);

CREATE TABLE IF NOT EXISTS rom_status (
    rom_id        INTEGER PRIMARY KEY REFERENCES roms(id),
    status        TEXT NOT NULL,
    last_updated  TEXT NOT NULL DEFAULT (datetime('now'))
);

-- Indexes for fast hash lookups
CREATE INDEX IF NOT EXISTS idx_roms_sha1 ON roms(sha1);
CREATE INDEX IF NOT EXISTS idx_roms_md5 ON roms(md5);
CREATE INDEX IF NOT EXISTS idx_roms_crc32 ON roms(crc32);
CREATE INDEX IF NOT EXISTS idx_roms_sha256 ON roms(sha256);
CREATE INDEX IF NOT EXISTS idx_files_sha1 ON files(sha1);
CREATE INDEX IF NOT EXISTS idx_files_crc32 ON files(crc32);
CREATE INDEX IF NOT EXISTS idx_rom_status_status ON rom_status(status);
CREATE INDEX IF NOT EXISTS idx_games_dat_game_id ON games(dat_game_id);
CREATE INDEX IF NOT EXISTS idx_games_clone_of ON games(clone_of);
)SQL";

auto match_type_to_string(core::MatchType type) -> std::string_view {
  switch (type) {
    case core::MatchType::Exact:
      return "exact";
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
  auto stmt =
      prepare("INSERT INTO dat_versions (dat_id, system_id, name, version, source_url, checksum) "
              "VALUES (?1, ?2, ?3, ?4, ?5, ?6)");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_text(1, dat.dat_id);
  stmt->bind_int64(2, dat.system_id);
  stmt->bind_text(3, dat.name);
  stmt->bind_text(4, dat.version);
  stmt->bind_text(5, dat.source_url);
  stmt->bind_text(6, dat.checksum);
  stmt->execute();

  return last_insert_id();
}

auto Database::find_dat_version(std::string_view name, std::string_view version)
    -> Result<std::optional<core::DatVersion>> {
  auto stmt =
      prepare("SELECT id, dat_id, system_id, name, version, source_url, checksum, imported_at "
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
      .dat_id = stmt->column_text(1),
      .system_id = stmt->column_int64(2),
      .name = stmt->column_text(3),
      .version = stmt->column_text(4),
      .source_url = stmt->column_text(5),
      .checksum = stmt->column_text(6),
      .imported_at = stmt->column_text(7),
  };
}

auto Database::get_latest_dat_version(std::int64_t system_id)
    -> Result<std::optional<core::DatVersion>> {
  auto stmt =
      prepare("SELECT id, dat_id, system_id, name, version, source_url, checksum, imported_at "
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
      .dat_id = stmt->column_text(1),
      .system_id = stmt->column_int64(2),
      .name = stmt->column_text(3),
      .version = stmt->column_text(4),
      .source_url = stmt->column_text(5),
      .checksum = stmt->column_text(6),
      .imported_at = stmt->column_text(7),
  };
}

// ═══════════════════════════════════════════════════════════════
// Games CRUD
// ═══════════════════════════════════════════════════════════════

auto Database::insert_game(const core::GameInfo& game) -> Result<std::int64_t> {
  auto stmt = prepare(
      "INSERT OR IGNORE INTO games "
      "(dat_game_id, name, description, clone_of, category, game_id_text, system_id, "
      "dat_version_id) "
      "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_text(1, game.dat_game_id);
  stmt->bind_text(2, game.name);
  stmt->bind_text(3, game.description);
  stmt->bind_text(4, game.clone_of);
  stmt->bind_text(5, game.category);
  stmt->bind_text(6, game.game_id_text);
  stmt->bind_int64(7, game.system_id);
  stmt->bind_int64(8, game.dat_version_id);
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
  auto stmt = prepare(
      "SELECT id, dat_game_id, name, description, clone_of, category, game_id_text, "
      "system_id, dat_version_id FROM games WHERE dat_version_id = ?1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_int64(1, dat_version_id);

  std::vector<core::GameInfo> games;
  while (stmt->step()) {
    games.push_back({
        .id = stmt->column_int64(0),
        .dat_game_id = stmt->column_text(1),
        .name = stmt->column_text(2),
        .description = stmt->column_text(3),
        .clone_of = stmt->column_text(4),
        .category = stmt->column_text(5),
        .game_id_text = stmt->column_text(6),
        .system_id = stmt->column_int64(7),
        .dat_version_id = stmt->column_int64(8),
        .roms = {},
    });
  }
  return games;
}

// ═══════════════════════════════════════════════════════════════
// ROMs CRUD
// ═══════════════════════════════════════════════════════════════

auto Database::insert_rom(const core::RomInfo& rom) -> Result<std::int64_t> {
  auto stmt =
      prepare("INSERT INTO roms (game_id, name, size, crc32, md5, sha1, sha256, region, status, "
              "serial, header) "
              "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11)");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_int64(1, rom.game_id);
  stmt->bind_text(2, rom.name);
  stmt->bind_int64(3, rom.size);
  stmt->bind_text(4, rom.crc32);
  stmt->bind_text(5, rom.md5);
  stmt->bind_text(6, rom.sha1);
  stmt->bind_text(7, rom.sha256);
  stmt->bind_text(8, rom.region);
  stmt->bind_text(9, rom.status);
  stmt->bind_text(10, rom.serial);
  stmt->bind_text(11, rom.header);
  stmt->execute();

  return last_insert_id();
}

auto Database::find_rom_by_sha1(std::string_view sha1) -> Result<std::optional<core::RomInfo>> {
  auto stmt =
      prepare("SELECT id, game_id, name, size, crc32, md5, sha1, sha256, region, status, serial, "
              "header FROM roms WHERE sha1 = ?1 LIMIT 1");
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
      .status = stmt->column_text(9),
      .serial = stmt->column_text(10),
      .header = stmt->column_text(11),
  };
}

auto Database::find_rom_by_md5(std::string_view md5) -> Result<std::optional<core::RomInfo>> {
  auto stmt =
      prepare("SELECT id, game_id, name, size, crc32, md5, sha1, sha256, region, status, serial, "
              "header FROM roms WHERE md5 = ?1 LIMIT 1");
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
      .status = stmt->column_text(9),
      .serial = stmt->column_text(10),
      .header = stmt->column_text(11),
  };
}

auto Database::find_rom_by_crc32(std::string_view crc32) -> Result<std::vector<core::RomInfo>> {
  auto stmt =
      prepare("SELECT id, game_id, name, size, crc32, md5, sha1, sha256, region, status, serial, "
              "header FROM roms WHERE crc32 = ?1");
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
        .status = stmt->column_text(9),
        .serial = stmt->column_text(10),
        .header = stmt->column_text(11),
    });
  }
  return roms;
}

auto Database::get_all_roms_for_system(std::int64_t system_id)
    -> Result<std::vector<core::RomInfo>> {
  auto stmt = prepare(
      "SELECT r.id, r.game_id, r.name, r.size, r.crc32, r.md5, r.sha1, r.sha256, r.region, "
      "r.status, r.serial, r.header "
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
        .status = stmt->column_text(9),
        .serial = stmt->column_text(10),
        .header = stmt->column_text(11),
    });
  }
  return roms;
}

// ═══════════════════════════════════════════════════════════════
// Files CRUD
// ═══════════════════════════════════════════════════════════════

auto Database::upsert_file(const core::FileInfo& file) -> Result<std::int64_t> {
  auto stmt = prepare("INSERT INTO files (path, size, crc32, md5, sha1, last_scanned) "
                      "VALUES (?1, ?2, ?3, ?4, ?5, datetime('now')) "
                      "ON CONFLICT(path) DO UPDATE SET "
                      "size = excluded.size, crc32 = excluded.crc32, md5 = excluded.md5, "
                      "sha1 = excluded.sha1, last_scanned = datetime('now')");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_text(1, file.path);
  stmt->bind_int64(2, file.size);
  stmt->bind_text(3, file.crc32);
  stmt->bind_text(4, file.md5);
  stmt->bind_text(5, file.sha1);
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
      prepare("SELECT id, path, size, crc32, md5, sha1, last_scanned FROM files WHERE path = ?1");
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
      .size = stmt->column_int64(2),
      .crc32 = stmt->column_text(3),
      .md5 = stmt->column_text(4),
      .sha1 = stmt->column_text(5),
      .last_scanned = stmt->column_text(6),
  };
}

auto Database::get_all_files() -> Result<std::vector<core::FileInfo>> {
  auto stmt = prepare("SELECT id, path, size, crc32, md5, sha1, last_scanned FROM files");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  std::vector<core::FileInfo> files;
  while (stmt->step()) {
    files.push_back({
        .id = stmt->column_int64(0),
        .path = stmt->column_text(1),
        .size = stmt->column_int64(2),
        .crc32 = stmt->column_text(3),
        .md5 = stmt->column_text(4),
        .sha1 = stmt->column_text(5),
        .last_scanned = stmt->column_text(6),
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

  // Also clean up related file_matches
  auto del_matches = prepare("DELETE FROM file_matches WHERE file_id = ?1");
  if (!del_matches) {
    return std::unexpected(del_matches.error());
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
      del_matches->bind_int64(1, file.id);
      del_matches->execute();
      del_matches->reset();

      del_stmt->bind_int64(1, file.id);
      del_stmt->execute();
      del_stmt->reset();
      ++removed;
    }
  }
  return removed;
}

// ═══════════════════════════════════════════════════════════════
// File Matches CRUD
// ═══════════════════════════════════════════════════════════════

auto Database::insert_file_match(const core::MatchResult& match) -> Result<void> {
  auto stmt = prepare("INSERT OR REPLACE INTO file_matches (file_id, rom_id, match_type) "
                      "VALUES (?1, ?2, ?3)");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_int64(1, match.file_id);
  stmt->bind_int64(2, match.rom_id);
  stmt->bind_text(3, match_type_to_string(match.match_type));
  stmt->execute();

  return {};
}

auto Database::get_matches_for_file(std::int64_t file_id)
    -> Result<std::vector<core::MatchResult>> {
  auto stmt = prepare("SELECT file_id, rom_id, match_type FROM file_matches WHERE file_id = ?1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_int64(1, file_id);

  std::vector<core::MatchResult> matches;
  while (stmt->step()) {
    matches.push_back({
        .file_id = stmt->column_int64(0),
        .rom_id = stmt->column_int64(1),
        .match_type = string_to_match_type(stmt->column_text(2)),
    });
  }
  return matches;
}

auto Database::get_matches_for_rom(std::int64_t rom_id) -> Result<std::vector<core::MatchResult>> {
  auto stmt = prepare("SELECT file_id, rom_id, match_type FROM file_matches WHERE rom_id = ?1");
  if (!stmt) {
    return std::unexpected(stmt.error());
  }

  stmt->bind_int64(1, rom_id);

  std::vector<core::MatchResult> matches;
  while (stmt->step()) {
    matches.push_back({
        .file_id = stmt->column_int64(0),
        .rom_id = stmt->column_int64(1),
        .match_type = string_to_match_type(stmt->column_text(2)),
    });
  }
  return matches;
}

auto Database::clear_matches() -> Result<void> {
  return execute("DELETE FROM file_matches");
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
  std::string sql = "SELECT f.path, r.name, g.name "
                    "FROM file_matches fm "
                    "JOIN files f ON fm.file_id = f.id "
                    "JOIN roms r ON fm.rom_id = r.id "
                    "JOIN games g ON r.game_id = g.id "
                    "WHERE fm.rom_id IN ("
                    "  SELECT rom_id FROM file_matches GROUP BY rom_id HAVING COUNT(*) > 1"
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
  std::string sql = "SELECT f.id, f.path, f.size, f.crc32, f.md5, f.sha1, f.last_scanned "
                    "FROM files f "
                    "LEFT JOIN file_matches fm ON f.id = fm.file_id "
                    "WHERE fm.file_id IS NULL "
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
        .size = stmt->column_int64(2),
        .crc32 = stmt->column_text(3),
        .md5 = stmt->column_text(4),
        .sha1 = stmt->column_text(5),
        .last_scanned = stmt->column_text(6),
    });
  }
  return unverified;
}

} // namespace romulus::database
