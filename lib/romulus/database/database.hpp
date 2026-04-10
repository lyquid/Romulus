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
  auto operator=(PreparedStatement&& other) noexcept -> PreparedStatement&;

  PreparedStatement(const PreparedStatement&) = delete;
  auto operator=(const PreparedStatement&) -> PreparedStatement& = delete;

  [[nodiscard]] auto get() const -> sqlite3_stmt* {
    return stmt_;
  }

  void bind_int64(int index, std::int64_t value);
  void bind_text(int index, std::string_view value);
  void bind_blob(int index, const std::vector<uint8_t>& blob);
  void bind_null(int index);

  /// Steps the statement. Returns true if a row is available (SQLITE_ROW).
  [[nodiscard]] auto step() -> bool;

  /// Executes a non-query statement (INSERT/UPDATE/DELETE) discarding the row result.
  void execute();

  /// Resets the statement for reuse with new bindings.
  void reset();

  [[nodiscard]] auto column_int64(int index) const -> std::int64_t;
  [[nodiscard]] auto column_text(int index) const -> std::string;
  [[nodiscard]] auto column_optional_text(int index) const -> std::optional<std::string>;
  [[nodiscard]] auto column_blob(int index) const -> std::vector<uint8_t>;

private:
  sqlite3_stmt* stmt_ = nullptr;
};

/// RAII transaction guard — commits on destruction unless rolled back or released.
class TransactionGuard final {
public:
  explicit TransactionGuard(sqlite3* db);
  ~TransactionGuard();

  TransactionGuard(TransactionGuard&& other) noexcept;
  auto operator=(TransactionGuard&& other) noexcept -> TransactionGuard&;

  TransactionGuard(const TransactionGuard&) = delete;
  auto operator=(const TransactionGuard&) -> TransactionGuard& = delete;

  /// Explicitly commit the transaction.
  void commit();

  /// Explicitly rollback the transaction.
  void rollback();

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
  auto operator=(Database&& other) noexcept -> Database&;

  Database(const Database&) = delete;
  auto operator=(const Database&) -> Database& = delete;

  /// Creates an RAII transaction guard.
  [[nodiscard]] auto begin_transaction() -> TransactionGuard;

  // ── Systems ──────────────────────────────────────────────

  [[nodiscard]] auto insert_system(const core::SystemInfo& system) -> Result<std::int64_t>;
  [[nodiscard]] auto find_system_by_name(std::string_view name)
      -> Result<std::optional<core::SystemInfo>>;
  [[nodiscard]] auto get_all_systems() -> Result<std::vector<core::SystemInfo>>;
  [[nodiscard]] auto get_or_create_system(std::string_view name) -> Result<std::int64_t>;

  // ── DAT Versions ─────────────────────────────────────────

  [[nodiscard]] auto insert_dat_version(const core::DatVersion& dat) -> Result<std::int64_t>;
  [[nodiscard]] auto find_dat_version(std::string_view name, std::string_view version)
      -> Result<std::optional<core::DatVersion>>;
  [[nodiscard]] auto get_latest_dat_version(std::int64_t system_id)
      -> Result<std::optional<core::DatVersion>>;
  [[nodiscard]] auto get_all_dat_versions() -> Result<std::vector<core::DatVersion>>;
  [[nodiscard]] auto get_roms_for_dat_version(std::int64_t dat_version_id)
      -> Result<std::vector<core::RomInfo>>;

  // ── Games ────────────────────────────────────────────────

  [[nodiscard]] auto insert_game(const core::GameInfo& game) -> Result<std::int64_t>;
  [[nodiscard]] auto get_games_by_dat_version(std::int64_t dat_version_id)
      -> Result<std::vector<core::GameInfo>>;

  // ── ROMs ─────────────────────────────────────────────────

  [[nodiscard]] auto insert_rom(const core::RomInfo& rom) -> Result<std::int64_t>;
  [[nodiscard]] auto find_rom_by_sha256(std::string_view sha256)
      -> Result<std::optional<core::RomInfo>>;
  [[nodiscard]] auto find_rom_by_sha1(std::string_view sha1)
      -> Result<std::optional<core::RomInfo>>;
  [[nodiscard]] auto find_rom_by_md5(std::string_view md5) -> Result<std::optional<core::RomInfo>>;
  [[nodiscard]] auto find_rom_by_crc32(std::string_view crc32)
      -> Result<std::vector<core::RomInfo>>;
  [[nodiscard]] auto get_all_roms_for_system(std::int64_t system_id)
      -> Result<std::vector<core::RomInfo>>;

  // ── Files ────────────────────────────────────────────────

  [[nodiscard]] auto upsert_file(const core::FileInfo& file) -> Result<std::int64_t>;
  [[nodiscard]] auto find_file_by_path(std::string_view path)
      -> Result<std::optional<core::FileInfo>>;
  [[nodiscard]] auto get_all_files() -> Result<std::vector<core::FileInfo>>;
  /// Returns only the path strings of all indexed files — cheap skip-check helper.
  [[nodiscard]] auto get_all_file_paths() -> Result<std::vector<std::string>>;
  [[nodiscard]] auto remove_missing_files(const std::vector<std::string>& existing_paths)
      -> Result<std::int64_t>;

  // ── Global ROMs ──────────────────────────────────────────

  [[nodiscard]] auto upsert_global_rom(const core::GlobalRom& rom) -> Result<void>;
  [[nodiscard]] auto find_global_rom_by_sha256(std::string_view sha256)
      -> Result<std::optional<core::GlobalRom>>;
  [[nodiscard]] auto find_global_rom_by_sha1(std::string_view sha1)
      -> Result<std::optional<core::GlobalRom>>;
  [[nodiscard]] auto find_global_rom_by_md5(std::string_view md5)
      -> Result<std::optional<core::GlobalRom>>;
  [[nodiscard]] auto find_global_rom_by_crc32(std::string_view crc32)
      -> Result<std::vector<core::GlobalRom>>;
  [[nodiscard]] auto has_files_for_global_rom(std::string_view global_rom_sha1) -> Result<bool>;

  // ── ROM Matches ──────────────────────────────────────────

  [[nodiscard]] auto insert_rom_match(const core::MatchResult& match) -> Result<void>;
  [[nodiscard]] auto get_matches_for_rom(std::int64_t rom_id)
      -> Result<std::vector<core::MatchResult>>;
  [[nodiscard]] auto clear_matches() -> Result<void>;

  // ── ROM Status ───────────────────────────────────────────

  [[nodiscard]] auto upsert_rom_status(std::int64_t rom_id,
                                       core::RomStatusType status) -> Result<void>;
  [[nodiscard]] auto get_rom_status(std::int64_t rom_id)
      -> Result<std::optional<core::RomStatusEntry>>;
  [[nodiscard]] auto get_collection_summary(std::optional<std::int64_t> system_id = {})
      -> Result<core::CollectionSummary>;
  [[nodiscard]] auto get_missing_roms(std::optional<std::int64_t> system_id = {})
      -> Result<std::vector<core::MissingRom>>;
  [[nodiscard]] auto get_duplicate_files(std::optional<std::int64_t> system_id = {})
      -> Result<std::vector<core::DuplicateFile>>;
  [[nodiscard]] auto get_unverified_files(std::optional<std::int64_t> system_id = {})
      -> Result<std::vector<core::FileInfo>>;

  // ── Utilities ────────────────────────────────────────────

  /// Executes a raw SQL statement (for migrations/admin).
  [[nodiscard]] auto execute(std::string_view sql) -> Result<void>;

  /// Prepares a SQL statement for reuse.
  [[nodiscard]] auto prepare(std::string_view sql) -> Result<PreparedStatement>;

  /// Returns the last inserted row ID.
  [[nodiscard]] auto last_insert_id() const -> std::int64_t;

  // ── Scanned Directories ──────────────────────────────────

  [[nodiscard]] auto add_scanned_directory(std::string_view path) -> Result<core::ScannedDirectory>;
  [[nodiscard]] auto get_all_scanned_directories() -> Result<std::vector<core::ScannedDirectory>>;
  [[nodiscard]] auto remove_scanned_directory(std::int64_t id) -> Result<void>;

  // ── DB Explorer ──────────────────────────────────────────

  /// Returns the names of all user-defined tables (excludes sqlite_* internal tables).
  [[nodiscard]] auto get_table_names() -> Result<std::vector<std::string>>;

  /// Queries all rows from the named table (up to k_TableQueryRowLimit rows).
  /// Returns column headers and row data as strings.
  /// @param table_name Must be a name returned by get_table_names().
  [[nodiscard]] auto query_table_data(std::string_view table_name)
      -> Result<core::TableQueryResult>;

private:
  /// Runs the schema migration (CREATE TABLE IF NOT EXISTS).
  void run_migrations();

  /// Enables WAL mode and foreign keys.
  void configure_connection();

  sqlite3* db_ = nullptr;
};

} // namespace romulus::database
