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
  RomulusService& operator=(RomulusService&&) noexcept;

  RomulusService(const RomulusService&) = delete;
  RomulusService& operator=(const RomulusService&) = delete;

  /// Returns the filesystem path of the underlying database file.
  [[nodiscard]] std::filesystem::path get_db_path() const;

  // ── DAT Operations ───────────────────────────────────────

  /// Imports a local DAT file into the database.
  [[nodiscard]] Result<core::DatVersion> import_dat(const std::filesystem::path& path);

  // ── Scan Operations ──────────────────────────────────────

  /// Scans a directory for ROM files and hashes them.
  [[nodiscard]] Result<core::ScanReport> scan_directory(
      const std::filesystem::path& dir, std::optional<std::vector<std::string>> extensions = {});

  /// Runs matching + classification on all files.
  [[nodiscard]] Result<void> verify(std::optional<std::string> dat_name = {});

  /// Full pipeline: import DAT → scan → match → classify.
  [[nodiscard]] Result<void> full_sync(const std::filesystem::path& dat_path,
                                       const std::filesystem::path& rom_dir);

  // ── Queries ──────────────────────────────────────────────

  [[nodiscard]] Result<core::CollectionSummary> get_summary(
      std::optional<std::string> dat_name = {});
  [[nodiscard]] Result<std::vector<core::DatVersion>> list_dat_versions();
  [[nodiscard]] Result<std::vector<std::pair<core::RomInfo, core::RomStatusType>>>
  get_roms_with_status(std::int64_t dat_version_id);
  [[nodiscard]] Result<std::vector<core::MissingRom>> get_missing_roms(
      std::optional<std::string> dat_name = {});
  [[nodiscard]] Result<std::vector<core::FileInfo>> get_all_files();

  // ── Admin ────────────────────────────────────────────────

  /// Atomically deletes all data from the database (all tables).
  [[nodiscard]] Result<void> purge_database();

  // ── Scanned Directories ──────────────────────────────────

  /// Registers a directory path for ROM scanning (persisted in DB).
  [[nodiscard]] Result<core::ScannedDirectory> add_scan_directory(const std::filesystem::path& dir);

  /// Returns all registered scan directories.
  [[nodiscard]] Result<std::vector<core::ScannedDirectory>> get_scan_directories();

  /// Removes a registered scan directory by its database ID.
  [[nodiscard]] Result<void> remove_scan_directory(std::int64_t id);

  // ── Reports ──────────────────────────────────────────────

  [[nodiscard]] Result<std::string> generate_report(core::ReportType type,
                                                    core::ReportFormat format,
                                                    std::optional<std::string> dat_name = {});

  // ── DB Explorer ──────────────────────────────────────────

  /// Returns the names of all user-defined tables in the database.
  [[nodiscard]] Result<std::vector<std::string>> get_db_table_names();

  /// Queries all rows from the named table.
  /// Callers should be aware that large tables may incur significant query and memory cost.
  [[nodiscard]] Result<core::TableQueryResult> query_db_table(std::string_view table_name);

private:
  /// Resolves an optional DAT name to an optional ID.
  [[nodiscard]] Result<std::optional<std::int64_t>> resolve_optional_dat_id(
      const std::optional<std::string>& dat_name);

  /// Resolves a DAT version name to its most-recently-imported ID.
  [[nodiscard]] Result<std::int64_t> resolve_dat_version_id(const std::string& dat_name);

  std::filesystem::path db_path_;
  std::unique_ptr<database::Database> db_;
};

} // namespace romulus::service
