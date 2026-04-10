#pragma once

/// @file romulus_service.hpp
/// @brief High-level service facade — the single API for CLI and future web consumers.
/// All commands route through this class. No module internals are exposed.

#include "romulus/core/error.hpp"
#include "romulus/core/types.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace romulus::database {
class Database;
}

namespace romulus::service {

using romulus::core::Result;

/// The main entry point for all ROMULUS operations.
/// Both the CLI and future web/REST API consume only this interface.
class RomulusService final {
public:
  explicit RomulusService(const std::filesystem::path& db_path);
  ~RomulusService();

  RomulusService(RomulusService&&) noexcept;
  auto operator=(RomulusService&&) noexcept -> RomulusService&;

  RomulusService(const RomulusService&) = delete;
  auto operator=(const RomulusService&) -> RomulusService& = delete;

  /// Returns the filesystem path of the underlying database file.
  [[nodiscard]] auto get_db_path() const -> std::filesystem::path;

  // ── DAT Operations ───────────────────────────────────────

  /// Imports a local DAT file into the database.
  [[nodiscard]] auto import_dat(const std::filesystem::path& path) -> Result<core::DatVersion>;

  // ── Scan Operations ──────────────────────────────────────

  /// Scans a directory for ROM files and hashes them.
  [[nodiscard]] auto scan_directory(const std::filesystem::path& dir,
                                    std::optional<std::vector<std::string>> extensions = {})
      -> Result<core::ScanReport>;

  /// Runs matching + classification on all files.
  [[nodiscard]] auto verify(std::optional<std::string> system = {}) -> Result<void>;

  /// Full pipeline: import DAT → scan → match → classify.
  [[nodiscard]] auto full_sync(const std::filesystem::path& dat_path,
                               const std::filesystem::path& rom_dir) -> Result<void>;

  // ── Queries ──────────────────────────────────────────────

  [[nodiscard]] auto get_summary(std::optional<std::string> system = {})
      -> Result<core::CollectionSummary>;
  [[nodiscard]] auto list_systems() -> Result<std::vector<core::SystemInfo>>;
  [[nodiscard]] auto list_dat_versions() -> Result<std::vector<core::DatVersion>>;
  [[nodiscard]] auto get_roms_with_status(std::int64_t dat_version_id)
      -> Result<std::vector<std::pair<core::RomInfo, core::RomStatusType>>>;
  [[nodiscard]] auto get_missing_roms(std::optional<std::string> system = {})
      -> Result<std::vector<core::MissingRom>>;
  [[nodiscard]] auto get_all_files() -> Result<std::vector<core::FileInfo>>;

  // ── Admin ────────────────────────────────────────────────

  /// Atomically deletes all data from the database (all tables).
  [[nodiscard]] auto purge_database() -> Result<void>;

  // ── Scanned Directories ──────────────────────────────────

  /// Registers a directory path for ROM scanning (persisted in DB).
  [[nodiscard]] auto add_scan_directory(const std::filesystem::path& dir)
      -> Result<core::ScannedDirectory>;

  /// Returns all registered scan directories.
  [[nodiscard]] auto get_scan_directories() -> Result<std::vector<core::ScannedDirectory>>;

  /// Removes a registered scan directory by its database ID.
  [[nodiscard]] auto remove_scan_directory(std::int64_t id) -> Result<void>;

  // ── Reports ──────────────────────────────────────────────

  [[nodiscard]] auto generate_report(core::ReportType type,
                                     core::ReportFormat format,
                                     std::optional<std::string> system = {}) -> Result<std::string>;

  // ── DB Explorer ──────────────────────────────────────────

  /// Returns the names of all user-defined tables in the database.
  [[nodiscard]] auto get_db_table_names() -> Result<std::vector<std::string>>;

  /// Queries all rows from the named table.
  /// Callers should be aware that large tables may incur significant query and memory cost.
  [[nodiscard]] auto query_db_table(std::string_view table_name)
      -> Result<core::TableQueryResult>;

private:
  /// Resolves a system name to its ID. Returns nullopt if no filter.
  [[nodiscard]] auto resolve_system_id(const std::optional<std::string>& system)
      -> Result<std::optional<std::int64_t>>;

  std::filesystem::path db_path_;
  std::unique_ptr<database::Database> db_;
};

} // namespace romulus::service
