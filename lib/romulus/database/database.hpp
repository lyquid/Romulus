#pragma once

/// @file database.hpp
/// @brief RAII SQLite database wrapper with migrations, transactions, and CRUD operations.
/// The database is the single source of truth for ROMULUS.

#include "romulus/core/error.hpp"
#include "romulus/core/types.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace romulus::database {

using romulus::core::Error;
using romulus::core::Result;

/// RAII wrapper for sqlite3_stmt — automatically finalizes on destruction.
class PreparedStatement final {
public:
  explicit PreparedStatement(sqlite3_stmt* stmt);
  ~PreparedStatement();

  PreparedStatement(PreparedStatement&& other) noexcept;
  PreparedStatement& operator=(PreparedStatement&& other) noexcept;

  PreparedStatement(const PreparedStatement&) = delete;
  PreparedStatement& operator=(const PreparedStatement&) = delete;

  [[nodiscard]] sqlite3_stmt* get() const {
    return stmt_;
  }

  void bind_int64(int index, std::int64_t value);
  void bind_text(int index, std::string_view value);
  void bind_blob(int index, const std::vector<uint8_t>& blob);
  void bind_null(int index);

  /// Steps the statement. Returns true if a row is available (SQLITE_ROW).
  [[nodiscard]] bool step();

  /// Executes a non-query statement (INSERT/UPDATE/DELETE) discarding the row result.
  void execute();

  /// Resets the statement for reuse with new bindings.
  void reset();

  [[nodiscard]] std::int64_t column_int64(int index) const;
  [[nodiscard]] std::string column_text(int index) const;
  [[nodiscard]] std::optional<std::string> column_optional_text(int index) const;
  [[nodiscard]] std::vector<uint8_t> column_blob(int index) const;

  /// Returns a display-friendly string for the column value at @p index.
  /// BLOBs are rendered as lowercase hex strings; NULLs become "(NULL)".
  /// INTEGER, FLOAT, and TEXT are returned as-is via sqlite3_column_text.
  [[nodiscard]] std::string column_display_text(int index) const;

private:
  sqlite3_stmt* stmt_ = nullptr;
};

/// RAII transaction guard — commits on destruction unless rolled back or released.
class TransactionGuard final {
public:
  /// Wraps an already-started SQLite transaction.
  /// BEGIN is executed by Database::begin_transaction() so begin errors can be reported.
  explicit TransactionGuard(sqlite3* db);
  ~TransactionGuard();

  TransactionGuard(TransactionGuard&& other) noexcept;
  TransactionGuard& operator=(TransactionGuard&& other) noexcept;

  TransactionGuard(const TransactionGuard&) = delete;
  TransactionGuard& operator=(const TransactionGuard&) = delete;

  /// Explicitly commit the transaction.
  [[nodiscard]] Result<void> commit();

  /// Explicitly rollback the transaction.
  [[nodiscard]] Result<void> rollback();

private:
  sqlite3* db_ = nullptr;
  bool committed_ = false;
};

/// Main database class — owns the SQLite connection and provides all CRUD operations.
/// This is the single source of truth for ROMULUS state.
class Database final {
public:
  /// Opens (or creates) the database at the given path and runs migrations.
  explicit Database(const std::filesystem::path& db_path);
  ~Database();

  Database(Database&& other) noexcept;
  Database& operator=(Database&& other) noexcept;

  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  /// Creates an RAII transaction guard.
  [[nodiscard]] Result<TransactionGuard> begin_transaction();

  // ── DAT Versions ─────────────────────────────────────────

  [[nodiscard]] Result<std::int64_t> insert_dat_version(const core::DatVersion& dat);
  [[nodiscard]] Result<std::optional<core::DatVersion>> find_dat_version(std::string_view name,
                                                                         std::string_view version);
  [[nodiscard]] Result<std::optional<core::DatVersion>> find_dat_version_by_sha256(
      std::string_view dat_sha256);
  [[nodiscard]] Result<std::optional<core::DatVersion>> find_dat_version_by_name(
      std::string_view name);
  [[nodiscard]] Result<std::vector<core::DatVersion>> get_all_dat_versions();
  [[nodiscard]] Result<std::vector<core::RomInfo>> get_roms_for_dat_version(
      std::int64_t dat_version_id);
  [[nodiscard]] Result<void> delete_dat_version(std::int64_t id);

  // ── Games ─────────────────────────────────────────────────

  /// Inserts a new game with the given dat_version_id and name, or returns the existing
  /// game's id if a game with the same (dat_version_id, name) already exists.
  [[nodiscard]] Result<std::int64_t> find_or_insert_game(std::int64_t dat_version_id,
                                                         std::string_view name);
  [[nodiscard]] Result<std::vector<core::GameEntry>> get_games_for_dat_version(
      std::int64_t dat_version_id);

  // ── ROMs ─────────────────────────────────────────────────

  [[nodiscard]] Result<std::int64_t> insert_rom(const core::RomInfo& rom);
  [[nodiscard]] Result<std::optional<core::RomInfo>> find_rom_by_sha256(std::string_view sha256);
  [[nodiscard]] Result<std::optional<core::RomInfo>> find_rom_by_sha1(std::string_view sha1);
  [[nodiscard]] Result<std::optional<core::RomInfo>> find_rom_by_md5(std::string_view md5);
  [[nodiscard]] Result<std::vector<core::RomInfo>> find_rom_by_crc32(std::string_view crc32);
  [[nodiscard]] Result<std::vector<core::RomInfo>> get_all_roms();

  // ── Files ────────────────────────────────────────────────

  [[nodiscard]] Result<std::int64_t> upsert_file(const core::FileInfo& file);
  [[nodiscard]] Result<std::optional<core::FileInfo>> find_file_by_path(std::string_view path);
  [[nodiscard]] Result<std::vector<core::FileInfo>> get_all_files();
  /// Returns lightweight file metadata (sha1, path, entry_name, last_write_time) for all
  /// files. Used by the matcher's CRC32 tiebreaker to avoid loading full BLOB hash columns.
  [[nodiscard]] Result<std::vector<core::FileTiebreakInfo>> get_file_tiebreak_info();
  /// Returns a map of virtual path → FileFingerprint (size + last_write_time).
  /// Used by the service layer to build the skip-check predicate: a file is skipped
  /// only when its current size and last_write_time both match the stored values.
  [[nodiscard]] Result<core::FingerprintMap> get_file_fingerprints();
  [[nodiscard]] Result<std::int64_t> remove_missing_files(
      const std::vector<std::string>& existing_paths);
  /// Removes files table rows whose virtual path is in \p paths.
  /// More efficient than remove_missing_files() when the exact set of rows to delete is
  /// already known: deletes by the indexed \c path column in a single pass without loading
  /// any blob columns.
  [[nodiscard]] Result<std::int64_t> remove_files_by_virtual_paths(
      const std::vector<std::string>& paths);

  // ── Global ROMs ──────────────────────────────────────────

  [[nodiscard]] Result<void> upsert_global_rom(const core::GlobalRom& rom);
  [[nodiscard]] Result<std::optional<core::GlobalRom>> find_global_rom_by_sha256(
      std::string_view sha256);
  [[nodiscard]] Result<std::optional<core::GlobalRom>> find_global_rom_by_sha1(
      std::string_view sha1);
  [[nodiscard]] Result<std::optional<core::GlobalRom>> find_global_rom_by_md5(std::string_view md5);
  [[nodiscard]] Result<std::vector<core::GlobalRom>> find_global_rom_by_crc32(
      std::string_view crc32);
  [[nodiscard]] Result<std::vector<core::GlobalRom>> get_all_global_roms();
  [[nodiscard]] Result<bool> has_files_for_global_rom(std::string_view global_rom_sha1);

  // ── ROM Matches ──────────────────────────────────────────

  [[nodiscard]] Result<void> insert_rom_match(const core::MatchResult& match);
  [[nodiscard]] Result<std::vector<core::MatchResult>> get_matches_for_rom(std::int64_t rom_id);
  [[nodiscard]] Result<void> clear_matches();

  // ── Status (cached + computed from rom_matches + files) ──────

  /// Recomputes ROM statuses and stores results in rom_status_cache.
  /// When dat_version_id is provided, only ROMs belonging to that DAT are
  /// refreshed (more efficient for per-DAT verify). When omitted, all ROMs are
  /// refreshed. Call this BEFORE classify_all() inside verify() so that
  /// get_collection_summary() always reads fresh data.
  [[nodiscard]] Result<void> refresh_status_cache(
      std::optional<std::int64_t> dat_version_id = {});

  /// Computes the status of a single ROM. Reads from rom_status_cache when
  /// populated; falls back to querying rom_matches + files on a cache miss.
  [[nodiscard]] Result<core::RomStatusType> get_computed_rom_status(std::int64_t rom_id);
  [[nodiscard]] Result<core::CollectionSummary> get_collection_summary(
      std::optional<std::int64_t> dat_version_id = {});
  [[nodiscard]] Result<std::vector<core::MissingRom>> get_missing_roms(
      std::optional<std::int64_t> dat_version_id = {});
  [[nodiscard]] Result<std::vector<core::DuplicateFile>> get_duplicate_files(
      std::optional<std::int64_t> dat_version_id = {});
  [[nodiscard]] Result<std::vector<core::FileInfo>> get_unverified_files();

  /// Returns all ROMs for the given DAT version together with their status in a
  /// single batch query. Reads from rom_status_cache when populated; inlines the
  /// status computation otherwise. Prefer this over calling get_computed_rom_status()
  /// in a loop (eliminates the N+1 query pattern).
  [[nodiscard]] Result<std::vector<std::pair<core::RomInfo, core::RomStatusType>>>
  get_all_roms_with_status(std::int64_t dat_version_id);

  // ── Utilities ────────────────────────────────────────────

  /// Executes a raw SQL statement (for migrations/admin).
  [[nodiscard]] Result<void> execute(std::string_view sql);

  /// Prepares a SQL statement for reuse.
  [[nodiscard]] Result<PreparedStatement> prepare(std::string_view sql);

  /// Returns the last inserted row ID.
  [[nodiscard]] std::int64_t last_insert_id() const;

  // ── Scanned Directories ──────────────────────────────────

  [[nodiscard]] Result<core::ScannedDirectory> add_scanned_directory(std::string_view path);
  [[nodiscard]] Result<std::vector<core::ScannedDirectory>> get_all_scanned_directories();
  [[nodiscard]] Result<void> remove_scanned_directory(std::int64_t id);

  // ── DB Explorer ──────────────────────────────────────────

  /// Returns the names of all user-defined tables (excludes sqlite_* internal tables).
  [[nodiscard]] Result<std::vector<std::string>> get_table_names();

  /// Queries all rows from the named table.
  /// Returns column metadata (including PK / NN / UQ / FK flags) and row data.
  /// BLOB columns are rendered as lowercase hex strings; NULLs as "(NULL)".
  /// @param table_name Must be a name returned by get_table_names().
  [[nodiscard]] Result<core::TableQueryResult> query_table_data(std::string_view table_name);

private:
  /// Runs the schema migration (CREATE TABLE IF NOT EXISTS).
  void run_migrations();

  /// Enables WAL mode and foreign keys.
  void configure_connection();

  sqlite3* db_ = nullptr;
};

} // namespace romulus::database
