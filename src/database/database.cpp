#include "romulus/database/database.hpp"

#include <sqlite3.h>
#include <spdlog/spdlog.h>

#include <cstring>
#include <utility>

namespace romulus {

namespace {

constexpr const char* kSchema = R"sql(
CREATE TABLE IF NOT EXISTS dat_versions (
  id          INTEGER PRIMARY KEY AUTOINCREMENT,
  name        TEXT NOT NULL,
  version     TEXT NOT NULL,
  source_url  TEXT NOT NULL,
  checksum    TEXT NOT NULL,
  imported_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS games (
  id             INTEGER PRIMARY KEY AUTOINCREMENT,
  name           TEXT NOT NULL,
  system         TEXT NOT NULL,
  dat_version_id INTEGER NOT NULL REFERENCES dat_versions(id)
);

CREATE TABLE IF NOT EXISTS roms (
  id      INTEGER PRIMARY KEY AUTOINCREMENT,
  game_id INTEGER NOT NULL REFERENCES games(id),
  name    TEXT NOT NULL,
  crc32   TEXT NOT NULL DEFAULT '',
  md5     TEXT NOT NULL DEFAULT '',
  sha1    TEXT NOT NULL DEFAULT '',
  region  TEXT NOT NULL DEFAULT ''
);

CREATE TABLE IF NOT EXISTS files (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  path         TEXT NOT NULL UNIQUE,
  size         INTEGER NOT NULL,
  crc32        TEXT NOT NULL DEFAULT '',
  md5          TEXT NOT NULL DEFAULT '',
  sha1         TEXT NOT NULL DEFAULT '',
  last_scanned TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS file_matches (
  file_id    INTEGER NOT NULL REFERENCES files(id),
  rom_id     INTEGER NOT NULL REFERENCES roms(id),
  match_type TEXT NOT NULL,
  PRIMARY KEY (file_id, rom_id)
);

CREATE TABLE IF NOT EXISTS rom_status (
  rom_id       INTEGER PRIMARY KEY REFERENCES roms(id),
  status       TEXT NOT NULL,
  last_updated TEXT NOT NULL
);
)sql";

const char* match_type_to_str(MatchType mt) noexcept {
  switch (mt) {
    case MatchType::Exact:
      return "Exact";
    case MatchType::CrcOnly:
      return "CrcOnly";
    case MatchType::Renamed:
      return "Renamed";
  }
  return "Exact";
}

MatchType str_to_match_type(const char* s) noexcept {
  if (s == nullptr) return MatchType::Exact;
  if (std::strcmp(s, "CrcOnly") == 0) return MatchType::CrcOnly;
  if (std::strcmp(s, "Renamed") == 0) return MatchType::Renamed;
  return MatchType::Exact;
}

const char* rom_status_to_str(RomStatus rs) noexcept {
  switch (rs) {
    case RomStatus::Have:
      return "Have";
    case RomStatus::Missing:
      return "Missing";
    case RomStatus::Duplicate:
      return "Duplicate";
    case RomStatus::BadDump:
      return "BadDump";
  }
  return "Missing";
}

RomStatus str_to_rom_status(const char* s) noexcept {
  if (s == nullptr) return RomStatus::Missing;
  if (std::strcmp(s, "Have") == 0) return RomStatus::Have;
  if (std::strcmp(s, "Duplicate") == 0) return RomStatus::Duplicate;
  if (std::strcmp(s, "BadDump") == 0) return RomStatus::BadDump;
  return RomStatus::Missing;
}

const char* col_text(sqlite3_stmt* stmt, int col) noexcept {
  const auto* raw = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
  return (raw != nullptr) ? raw : "";
}

}  // namespace

Database::Database(std::filesystem::path db_path) : db_path_(std::move(db_path)) {}

Database::~Database() {
  if (db_ != nullptr) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

Database::Database(Database&& other) noexcept
    : db_path_(std::move(other.db_path_)), db_(other.db_) {
  other.db_ = nullptr;
}

Database& Database::operator=(Database&& other) noexcept {
  if (this != &other) {
    if (db_ != nullptr) sqlite3_close(db_);
    db_path_ = std::move(other.db_path_);
    db_ = other.db_;
    other.db_ = nullptr;
  }
  return *this;
}

std::expected<void, Error> Database::initialize() {
  const int rc = sqlite3_open(db_path_.string().c_str(), &db_);
  if (rc != SQLITE_OK) {
    spdlog::error("Database::initialize: failed to open '{}': {}", db_path_.string(),
                  sqlite3_errmsg(db_));
    return std::unexpected(Error::DatabaseOpenFailed);
  }
  auto res = exec_sql(kSchema);
  if (!res) {
    spdlog::error("Database::initialize: schema migration failed");
    return std::unexpected(Error::DatabaseSchemaMigrationFailed);
  }
  spdlog::info("Database initialized at '{}'", db_path_.string());
  return {};
}

std::expected<void, Error> Database::exec_sql(const char* sql) {
  char* errmsg = nullptr;
  const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &errmsg);
  if (rc != SQLITE_OK) {
    spdlog::error("exec_sql failed: {}", errmsg != nullptr ? errmsg : "unknown");
    sqlite3_free(errmsg);
    return std::unexpected(Error::DatabaseQueryFailed);
  }
  return {};
}

std::expected<void, Error> Database::begin_transaction() {
  return exec_sql("BEGIN TRANSACTION;");
}

std::expected<void, Error> Database::commit() { return exec_sql("COMMIT;"); }

std::expected<void, Error> Database::rollback() { return exec_sql("ROLLBACK;"); }

// ---- DatVersion ----

std::expected<Id, Error> Database::insert_dat_version(const DatVersion& dv) {
  const char* sql =
      "INSERT INTO dat_versions (name, version, source_url, checksum, imported_at) "
      "VALUES (?, ?, ?, ?, ?);";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::unexpected(Error::DatabaseQueryFailed);
  }
  sqlite3_bind_text(stmt, 1, dv.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, dv.version.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, dv.source_url.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, dv.checksum.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, dv.imported_at.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return std::unexpected(Error::DatabaseQueryFailed);
  }
  const Id inserted_id = static_cast<Id>(sqlite3_last_insert_rowid(db_));
  sqlite3_finalize(stmt);
  return inserted_id;
}

std::expected<std::vector<DatVersion>, Error> Database::query_dat_versions() {
  const char* sql = "SELECT id, name, version, source_url, checksum, imported_at FROM dat_versions;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::unexpected(Error::DatabaseQueryFailed);
  }
  std::vector<DatVersion> results;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    DatVersion dv;
    dv.id = static_cast<Id>(sqlite3_column_int64(stmt, 0));
    dv.name = col_text(stmt, 1);
    dv.version = col_text(stmt, 2);
    dv.source_url = col_text(stmt, 3);
    dv.checksum = col_text(stmt, 4);
    dv.imported_at = col_text(stmt, 5);
    results.push_back(std::move(dv));
  }
  sqlite3_finalize(stmt);
  return results;
}

// ---- Game ----

std::expected<Id, Error> Database::insert_game(const Game& game) {
  const char* sql =
      "INSERT INTO games (name, system, dat_version_id) VALUES (?, ?, ?);";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::unexpected(Error::DatabaseQueryFailed);
  }
  sqlite3_bind_text(stmt, 1, game.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, game.system.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(game.dat_version_id));
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return std::unexpected(Error::DatabaseQueryFailed);
  }
  const Id inserted_id = static_cast<Id>(sqlite3_last_insert_rowid(db_));
  sqlite3_finalize(stmt);
  return inserted_id;
}

std::expected<std::vector<Game>, Error> Database::query_all_games() {
  const char* sql = "SELECT id, name, system, dat_version_id FROM games;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::unexpected(Error::DatabaseQueryFailed);
  }
  std::vector<Game> results;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    Game g;
    g.id = static_cast<Id>(sqlite3_column_int64(stmt, 0));
    g.name = col_text(stmt, 1);
    g.system = col_text(stmt, 2);
    g.dat_version_id = static_cast<Id>(sqlite3_column_int64(stmt, 3));
    results.push_back(std::move(g));
  }
  sqlite3_finalize(stmt);
  return results;
}

std::expected<std::vector<Game>, Error> Database::query_games_by_dat_version(
    Id dat_version_id) {
  const char* sql = "SELECT id, name, system, dat_version_id FROM games WHERE dat_version_id = ?;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::unexpected(Error::DatabaseQueryFailed);
  }
  sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(dat_version_id));
  std::vector<Game> results;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    Game g;
    g.id = static_cast<Id>(sqlite3_column_int64(stmt, 0));
    g.name = col_text(stmt, 1);
    g.system = col_text(stmt, 2);
    g.dat_version_id = static_cast<Id>(sqlite3_column_int64(stmt, 3));
    results.push_back(std::move(g));
  }
  sqlite3_finalize(stmt);
  return results;
}

// ---- Rom ----

std::expected<Id, Error> Database::insert_rom(const Rom& rom) {
  const char* sql =
      "INSERT INTO roms (game_id, name, crc32, md5, sha1, region) VALUES (?, ?, ?, ?, ?, ?);";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::unexpected(Error::DatabaseQueryFailed);
  }
  sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(rom.game_id));
  sqlite3_bind_text(stmt, 2, rom.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, rom.crc32.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, rom.md5.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, rom.sha1.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, rom.region.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return std::unexpected(Error::DatabaseQueryFailed);
  }
  const Id inserted_id = static_cast<Id>(sqlite3_last_insert_rowid(db_));
  sqlite3_finalize(stmt);
  return inserted_id;
}

std::expected<std::vector<Rom>, Error> Database::query_roms_by_game(Id game_id) {
  const char* sql =
      "SELECT id, game_id, name, crc32, md5, sha1, region FROM roms WHERE game_id = ?;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::unexpected(Error::DatabaseQueryFailed);
  }
  sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(game_id));
  std::vector<Rom> results;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    Rom r;
    r.id = static_cast<Id>(sqlite3_column_int64(stmt, 0));
    r.game_id = static_cast<Id>(sqlite3_column_int64(stmt, 1));
    r.name = col_text(stmt, 2);
    r.crc32 = col_text(stmt, 3);
    r.md5 = col_text(stmt, 4);
    r.sha1 = col_text(stmt, 5);
    r.region = col_text(stmt, 6);
    results.push_back(std::move(r));
  }
  sqlite3_finalize(stmt);
  return results;
}

std::expected<std::vector<Rom>, Error> Database::query_all_roms() {
  const char* sql = "SELECT id, game_id, name, crc32, md5, sha1, region FROM roms;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::unexpected(Error::DatabaseQueryFailed);
  }
  std::vector<Rom> results;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    Rom r;
    r.id = static_cast<Id>(sqlite3_column_int64(stmt, 0));
    r.game_id = static_cast<Id>(sqlite3_column_int64(stmt, 1));
    r.name = col_text(stmt, 2);
    r.crc32 = col_text(stmt, 3);
    r.md5 = col_text(stmt, 4);
    r.sha1 = col_text(stmt, 5);
    r.region = col_text(stmt, 6);
    results.push_back(std::move(r));
  }
  sqlite3_finalize(stmt);
  return results;
}

// ---- ScannedFile ----

std::expected<Id, Error> Database::upsert_scanned_file(const ScannedFile& sf) {
  const char* sql =
      "INSERT INTO files (path, size, crc32, md5, sha1, last_scanned) "
      "VALUES (?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(path) DO UPDATE SET "
      "size=excluded.size, crc32=excluded.crc32, md5=excluded.md5, "
      "sha1=excluded.sha1, last_scanned=excluded.last_scanned;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::unexpected(Error::DatabaseQueryFailed);
  }
  const std::string path_str = sf.path.string();
  sqlite3_bind_text(stmt, 1, path_str.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(sf.size));
  sqlite3_bind_text(stmt, 3, sf.crc32.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, sf.md5.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, sf.sha1.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, sf.last_scanned.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return std::unexpected(Error::DatabaseQueryFailed);
  }
  const Id inserted_id = static_cast<Id>(sqlite3_last_insert_rowid(db_));
  sqlite3_finalize(stmt);
  return inserted_id;
}

std::expected<std::vector<ScannedFile>, Error> Database::query_scanned_files() {
  const char* sql =
      "SELECT id, path, size, crc32, md5, sha1, last_scanned FROM files;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::unexpected(Error::DatabaseQueryFailed);
  }
  std::vector<ScannedFile> results;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    ScannedFile sf;
    sf.id = static_cast<Id>(sqlite3_column_int64(stmt, 0));
    sf.path = col_text(stmt, 1);
    sf.size = static_cast<std::int64_t>(sqlite3_column_int64(stmt, 2));
    sf.crc32 = col_text(stmt, 3);
    sf.md5 = col_text(stmt, 4);
    sf.sha1 = col_text(stmt, 5);
    sf.last_scanned = col_text(stmt, 6);
    results.push_back(std::move(sf));
  }
  sqlite3_finalize(stmt);
  return results;
}

// ---- FileMatch ----

std::expected<void, Error> Database::insert_file_match(const FileMatch& fm) {
  const char* sql =
      "INSERT OR REPLACE INTO file_matches (file_id, rom_id, match_type) VALUES (?, ?, ?);";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::unexpected(Error::DatabaseQueryFailed);
  }
  sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(fm.file_id));
  sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(fm.rom_id));
  sqlite3_bind_text(stmt, 3, match_type_to_str(fm.match_type), -1, SQLITE_STATIC);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return std::unexpected(Error::DatabaseQueryFailed);
  }
  sqlite3_finalize(stmt);
  return {};
}

std::expected<std::vector<FileMatch>, Error> Database::query_file_matches() {
  const char* sql = "SELECT file_id, rom_id, match_type FROM file_matches;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::unexpected(Error::DatabaseQueryFailed);
  }
  std::vector<FileMatch> results;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    FileMatch fm;
    fm.file_id = static_cast<Id>(sqlite3_column_int64(stmt, 0));
    fm.rom_id = static_cast<Id>(sqlite3_column_int64(stmt, 1));
    fm.match_type = str_to_match_type(col_text(stmt, 2));
    results.push_back(fm);
  }
  sqlite3_finalize(stmt);
  return results;
}

// ---- RomStatus ----

std::expected<void, Error> Database::upsert_rom_status(const RomStatusRecord& rs) {
  const char* sql =
      "INSERT INTO rom_status (rom_id, status, last_updated) VALUES (?, ?, ?) "
      "ON CONFLICT(rom_id) DO UPDATE SET status=excluded.status, "
      "last_updated=excluded.last_updated;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::unexpected(Error::DatabaseQueryFailed);
  }
  sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(rs.rom_id));
  sqlite3_bind_text(stmt, 2, rom_status_to_str(rs.status), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, rs.last_updated.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return std::unexpected(Error::DatabaseQueryFailed);
  }
  sqlite3_finalize(stmt);
  return {};
}

std::expected<std::vector<RomStatusRecord>, Error> Database::query_rom_statuses() {
  const char* sql = "SELECT rom_id, status, last_updated FROM rom_status;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::unexpected(Error::DatabaseQueryFailed);
  }
  std::vector<RomStatusRecord> results;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    RomStatusRecord rs;
    rs.rom_id = static_cast<Id>(sqlite3_column_int64(stmt, 0));
    rs.status = str_to_rom_status(col_text(stmt, 1));
    rs.last_updated = col_text(stmt, 2);
    results.push_back(rs);
  }
  sqlite3_finalize(stmt);
  return results;
}

}  // namespace romulus
