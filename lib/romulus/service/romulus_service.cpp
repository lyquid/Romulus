#include "romulus/service/romulus_service.hpp"

#include "romulus/core/logging.hpp"
#include "romulus/dat/dat_fetcher.hpp"
#include "romulus/dat/dat_parser.hpp"
#include "romulus/database/database.hpp"
#include "romulus/engine/classifier.hpp"
#include "romulus/engine/matcher.hpp"
#include "romulus/report/report_generator.hpp"
#include "romulus/scanner/rom_scanner.hpp"

#include <array>
#include <unordered_set>
#include <utility>

namespace romulus::service {

RomulusService::RomulusService(const std::filesystem::path& db_path)
    : db_path_(db_path), db_(std::make_unique<database::Database>(db_path)) {
  ROMULUS_INFO("ROMULUS service initialized");
}

RomulusService::~RomulusService() = default;
RomulusService::RomulusService(RomulusService&&) noexcept = default;
auto RomulusService::operator=(RomulusService&&) noexcept -> RomulusService& = default;

// ═══════════════════════════════════════════════════════════════
// DAT Operations
// ═══════════════════════════════════════════════════════════════

auto RomulusService::import_dat(const std::filesystem::path& path) -> Result<core::DatVersion> {
  // Validate file
  auto validated = dat::DatFetcher::validate_local(path);
  if (!validated) {
    return std::unexpected(validated.error());
  }

  // Compute checksum
  auto checksum = dat::DatFetcher::compute_checksum(*validated);
  if (!checksum) {
    return std::unexpected(checksum.error());
  }

  // Check if already imported by checksum (fast path — avoids full parse on re-import).
  // Two DAT files with the same checksum are treated as identical regardless of name/version;
  // this prevents re-importing a file that was merely renamed or repackaged.
  auto existing_by_checksum = db_->find_dat_version_by_checksum(*checksum);
  if (existing_by_checksum && existing_by_checksum->has_value()) {
    ROMULUS_INFO("DAT already imported (checksum match): '{}'",
                 existing_by_checksum->value().name);
    return existing_by_checksum->value();
  }

  // Parse DAT
  dat::DatParser parser;
  auto dat_file = parser.parse(*validated);
  if (!dat_file) {
    return std::unexpected(dat_file.error());
  }

  // Store DAT version
  core::DatVersion dat_version{
      .name = dat_file->header.name,
      .version = dat_file->header.version,
      .source_url = validated->string(),
      .checksum = *checksum,
      .imported_at = {},
  };

  auto dat_id = db_->insert_dat_version(dat_version);
  if (!dat_id) {
    return std::unexpected(dat_id.error());
  }
  dat_version.id = *dat_id;

  // Store ROMs in a transaction (games are denormalized into roms.game_name)
  auto txn = db_->begin_transaction();

  for (const auto& game : dat_file->games) {
    for (const auto& rom : game.roms) {
      core::RomInfo rom_entry{
          .dat_version_id = *dat_id,
          .game_name = game.name,
          .name = rom.name,
          .size = rom.size,
          .crc32 = rom.crc32,
          .md5 = rom.md5,
          .sha1 = rom.sha1,
          .sha256 = {},
          .region = rom.region,
      };

      auto rom_id = db_->insert_rom(rom_entry);
      if (!rom_id) {
        ROMULUS_WARN("Failed to insert ROM '{}': {}", rom.name, rom_id.error().message);
      }
    }
  }

  txn.commit();

  ROMULUS_INFO("Imported DAT: '{}' v{} — {} games",
               dat_file->header.name,
               dat_file->header.version,
               dat_file->games.size());

  return dat_version;
}

// ═══════════════════════════════════════════════════════════════
// Scan Operations
// ═══════════════════════════════════════════════════════════════

auto RomulusService::scan_directory(const std::filesystem::path& dir,
                                    std::optional<std::vector<std::string>> extensions)
    -> Result<core::ScanReport> {
  // Pre-load only file paths (not full FileInfo rows) so the scanner can skip already-stored
  // files without decoding unused hash columns.
  auto existing_paths = db_->get_all_file_paths();
  if (!existing_paths) {
    return std::unexpected(existing_paths.error());
  }
  // Use a transparent hash/equal to avoid a temporary std::string allocation on each lookup.
  struct StringHash {
    using is_transparent = void;
    auto operator()(std::string_view sv) const noexcept -> std::size_t {
      return std::hash<std::string_view>{}(sv);
    }
  };
  std::unordered_set<std::string, StringHash, std::equal_to<>> known_paths(
      std::make_move_iterator(existing_paths->begin()),
      std::make_move_iterator(existing_paths->end()));

  auto skip_fn = [&known_paths](std::string_view path) -> bool {
    return known_paths.contains(path);
  };

  // Run the scanner — no database access inside the scanner itself.
  auto result = scanner::RomScanner::scan(dir, skip_fn, std::move(extensions));
  if (!result) {
    return std::unexpected(result.error());
  }

  // Persist all newly discovered files in a single checked transaction.
  // On per-file upsert failure, we log and continue rather than rolling back the entire
  // transaction — losing one file's record is preferable to discarding all scan work.
  auto begin = db_->execute("BEGIN TRANSACTION");
  if (!begin) {
    return std::unexpected(begin.error());
  }

  for (const auto& rom : result->files) {
    const core::FileInfo file{
        .filename = rom.filename(),
        .path = rom.virtual_path(),
        .size = rom.size,
        .crc32 = rom.hash_digest.to_hex_crc32(),
        .md5 = rom.hash_digest.to_hex_md5(),
        .sha1 = rom.hash_digest.to_hex_sha1(),
        .sha256 = rom.hash_digest.to_hex_sha256(),
        .last_scanned = {},
        .is_archive_entry = rom.is_archive_entry(),
    };
    auto upsert = db_->upsert_file(file);
    if (!upsert) {
      ROMULUS_WARN("DB upsert failed for '{}': {}", file.path, upsert.error().message);
    }
  }

  auto commit = db_->execute("COMMIT");
  if (!commit) {
    auto rollback = db_->execute("ROLLBACK");
    if (!rollback) {
      ROMULUS_ERROR("Rollback failed after commit failure: {}", rollback.error().message);
    }
    return std::unexpected(commit.error());
  }

  return result->report;
}

auto RomulusService::verify(std::optional<std::string> dat_name) -> Result<void> {
  // Step 1: Match files to ROMs
  auto matches = engine::Matcher::match_all(*db_);
  if (!matches) {
    return std::unexpected(matches.error());
  }

  // Step 2: Classify and log ROM statuses
  std::optional<std::int64_t> dat_id;
  if (dat_name.has_value()) {
    auto dat_id_result = resolve_dat_version_id(*dat_name);
    if (!dat_id_result) {
      return std::unexpected(dat_id_result.error());
    }
    dat_id = *dat_id_result;
  }

  return engine::Classifier::classify_all(*db_, dat_id);
}

auto RomulusService::full_sync(const std::filesystem::path& dat_path,
                               const std::filesystem::path& rom_dir) -> Result<void> {
  ROMULUS_INFO("Starting full sync: DAT={}, ROM dir={}", dat_path.string(), rom_dir.string());

  // 1. Import DAT
  auto dat = import_dat(dat_path);
  if (!dat) {
    return std::unexpected(dat.error());
  }

  // 2. Scan directory
  auto scan = scan_directory(rom_dir);
  if (!scan) {
    return std::unexpected(scan.error());
  }

  // 3. Verify (match + classify)
  auto result = verify();
  if (!result) {
    return std::unexpected(result.error());
  }

  ROMULUS_INFO("Full sync complete");
  return {};
}

// ═══════════════════════════════════════════════════════════════
// Queries
// ═══════════════════════════════════════════════════════════════

auto RomulusService::get_summary(std::optional<std::string> dat_name)
    -> Result<core::CollectionSummary> {
  std::optional<std::int64_t> dat_id;
  if (dat_name.has_value()) {
    auto id_result = resolve_dat_version_id(*dat_name);
    if (!id_result) {
      return std::unexpected(id_result.error());
    }
    dat_id = *id_result;
  }
  return db_->get_collection_summary(dat_id);
}

auto RomulusService::list_dat_versions() -> Result<std::vector<core::DatVersion>> {
  return db_->get_all_dat_versions();
}

auto RomulusService::get_roms_with_status(std::int64_t dat_version_id)
    -> Result<std::vector<std::pair<core::RomInfo, core::RomStatusType>>> {
  auto roms = db_->get_roms_for_dat_version(dat_version_id);
  if (!roms) {
    return std::unexpected(roms.error());
  }

  std::vector<std::pair<core::RomInfo, core::RomStatusType>> result;
  result.reserve(roms->size());

  for (auto& rom : *roms) {
    auto status = db_->get_computed_rom_status(rom.id);
    core::RomStatusType st = core::RomStatusType::Missing;
    if (status) {
      st = *status;
    }
    result.emplace_back(std::move(rom), st);
  }

  return result;
}

auto RomulusService::get_missing_roms(std::optional<std::string> dat_name)
    -> Result<std::vector<core::MissingRom>> {
  std::optional<std::int64_t> dat_id;
  if (dat_name.has_value()) {
    auto id_result = resolve_dat_version_id(*dat_name);
    if (!id_result) {
      return std::unexpected(id_result.error());
    }
    dat_id = *id_result;
  }
  return db_->get_missing_roms(dat_id);
}

auto RomulusService::get_all_files() -> Result<std::vector<core::FileInfo>> {
  return db_->get_all_files();
}

// ═══════════════════════════════════════════════════════════════
// Admin
// ═══════════════════════════════════════════════════════════════

auto RomulusService::purge_database() -> Result<void> {
  static constexpr std::array k_Tables = {
      "rom_matches",
      "files",
      "global_roms",
      "roms",
      "dat_versions",
  };

  auto txn = db_->begin_transaction();
  for (const auto* table : k_Tables) {
    auto result = db_->execute(std::string("DELETE FROM ") + table);
    if (!result) {
      return std::unexpected(result.error());
    }
  }
  txn.commit();

  // Force WAL checkpoint so the main .db file reflects the purge immediately.
  // Without this, data may still appear in the DB file until SQLite checkpoints.
  auto wal = db_->execute("PRAGMA wal_checkpoint(TRUNCATE)");
  if (!wal) {
    ROMULUS_WARN("WAL checkpoint after purge failed: {}", wal.error().message);
  }

  ROMULUS_INFO("Database purged — all tables cleared");
  return {};
}

// ═══════════════════════════════════════════════════════════════
// Scanned Directories
// ═══════════════════════════════════════════════════════════════

auto RomulusService::add_scan_directory(const std::filesystem::path& dir)
    -> Result<core::ScannedDirectory> {
  return db_->add_scanned_directory(dir.string());
}

auto RomulusService::get_scan_directories() -> Result<std::vector<core::ScannedDirectory>> {
  return db_->get_all_scanned_directories();
}

auto RomulusService::remove_scan_directory(std::int64_t id) -> Result<void> {
  return db_->remove_scanned_directory(id);
}

// ═══════════════════════════════════════════════════════════════
// Reports
// ═══════════════════════════════════════════════════════════════

auto RomulusService::generate_report(core::ReportType type,
                                     core::ReportFormat format,
                                     std::optional<std::string> dat_name) -> Result<std::string> {
  std::optional<std::int64_t> dat_id;
  if (dat_name.has_value()) {
    auto id_result = resolve_dat_version_id(*dat_name);
    if (!id_result) {
      return std::unexpected(id_result.error());
    }
    dat_id = *id_result;
  }
  return report::ReportGenerator::generate(*db_, type, format, dat_id);
}

// ═══════════════════════════════════════════════════════════════
// DB Explorer
// ═══════════════════════════════════════════════════════════════

auto RomulusService::get_db_path() const -> std::filesystem::path {
  return db_path_;
}

auto RomulusService::get_db_table_names() -> Result<std::vector<std::string>> {
  return db_->get_table_names();
}

auto RomulusService::query_db_table(std::string_view table_name) -> Result<core::TableQueryResult> {
  return db_->query_table_data(table_name);
}

// ═══════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════

auto RomulusService::resolve_dat_version_id(const std::string& dat_name)
    -> Result<std::int64_t> {
  auto found = db_->find_dat_version_by_name(dat_name);
  if (!found) {
    return std::unexpected(found.error());
  }
  if (!found->has_value()) {
    return std::unexpected(
        core::Error{core::ErrorCode::NotFound, "DAT not found: '" + dat_name + "'"});
  }
  return found->value().id;
}

} // namespace romulus::service
