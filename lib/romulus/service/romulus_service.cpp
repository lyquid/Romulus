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
#include <utility>

namespace romulus::service {

RomulusService::RomulusService(const std::filesystem::path& db_path)
    : db_path_(db_path), db_(std::make_unique<database::Database>(db_path)) {
  ROMULUS_INFO("ROMULUS service initialized");
}

RomulusService::~RomulusService() = default;
RomulusService::RomulusService(RomulusService&&) noexcept = default;
RomulusService& RomulusService::operator=(RomulusService&&) noexcept = default;

// ═══════════════════════════════════════════════════════════════
// DAT Operations
// ═══════════════════════════════════════════════════════════════

Result<core::DatVersion> RomulusService::import_dat(const std::filesystem::path& path) {
  // Validate file
  auto validated = dat::DatFetcher::validate_local(path);
  if (!validated) {
    return std::unexpected(validated.error());
  }

  // Compute DAT SHA-256
  auto dat_sha256 = dat::DatFetcher::compute_sha256(*validated);
  if (!dat_sha256) {
    return std::unexpected(dat_sha256.error());
  }

  // Check if already imported by DAT SHA-256 (fast path — avoids full parse on re-import).
  // Two DAT files with the same SHA-256 are treated as identical regardless of name/version;
  // this prevents re-importing a file that was merely renamed or repackaged.
  auto existing_by_sha256 = db_->find_dat_version_by_sha256(*dat_sha256);
  if (!existing_by_sha256) {
    return std::unexpected(existing_by_sha256.error());
  }
  if (existing_by_sha256->has_value()) {
    ROMULUS_INFO("DAT already imported (SHA-256 match): '{}'", existing_by_sha256->value().name);
    return existing_by_sha256->value();
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
      .system = dat_file->header.description,
      .source_url = validated->string(),
      .dat_sha256 = *dat_sha256,
      .imported_at = {},
  };

  auto dat_id = db_->insert_dat_version(dat_version);
  if (!dat_id) {
    return std::unexpected(dat_id.error());
  }
  dat_version.id = *dat_id;

  // Store each game and its ROMs in a transaction.
  // Games are now normalized: one games row per (dat_version_id, name).
  auto txn = db_->begin_transaction();
  if (!txn) {
    return std::unexpected(txn.error());
  }

  for (const auto& game : dat_file->games) {
    auto game_id = db_->find_or_insert_game(*dat_id, game.name);
    if (!game_id) {
      ROMULUS_WARN("Failed to insert game '{}': {}", game.name, game_id.error().message);
      continue;
    }

    for (const auto& rom : game.roms) {
      core::RomInfo rom_entry{
          .game_id = *game_id,
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

  auto commit = txn->commit();
  if (!commit) {
    return std::unexpected(commit.error());
  }

  ROMULUS_INFO("Imported DAT: '{}' v{} — {} games",
               dat_file->header.name,
               dat_file->header.version,
               dat_file->games.size());

  return dat_version;
}

// ═══════════════════════════════════════════════════════════════
// Scan Operations
// ═══════════════════════════════════════════════════════════════

Result<core::ScanReport> RomulusService::scan_directory(
    const std::filesystem::path& dir, std::optional<std::vector<std::string>> extensions) {
  // Pre-load path → {size, last_write_time} fingerprints so the scanner can skip files that
  // are already indexed AND whose size and mtime haven't changed. This guards against cases
  // where a file has been replaced (same path, different content) or silently modified.
  auto fingerprints = db_->get_file_fingerprints();
  if (!fingerprints) {
    return std::unexpected(fingerprints.error());
  }

  // Skip only when the virtual path is known AND both size and mtime match the stored values.
  // The predicate is called concurrently — bind the map as const so all threads use the
  // const overload of find() (guaranteed safe for concurrent reads under the standard).
  // FingerprintMap uses a transparent hash so we can find() with string_view directly.
  const core::FingerprintMap& fp = fingerprints.value();
  auto skip_fn =
      [&fp](std::string_view path, std::int64_t size, std::int64_t last_write_time) -> bool {
    const auto it = fp.find(path);
    if (it == fp.end()) {
      return false; // new file — must hash
    }
    return it->second.size == size && it->second.last_write_time == last_write_time;
  };

  // Run the scanner — no database access inside the scanner itself.
  auto result = scanner::RomScanner::scan(dir, skip_fn, std::move(extensions));
  if (!result) {
    return std::unexpected(result.error());
  }

  // Persist all newly discovered files in a single checked transaction.
  // On per-file upsert failure, we log and continue rather than rolling back the entire
  // transaction — losing one file's record is preferable to discarding all scan work.
  auto txn = db_->begin_transaction();
  if (!txn) {
    return std::unexpected(txn.error());
  }

  for (const auto& rom : result->files) {
    const core::FileInfo file{
        .path = rom.virtual_path(),
        .archive_path = rom.is_archive_entry()
                            ? std::optional<std::string>{rom.archive_path.string()}
                            : std::nullopt,
        .entry_name = rom.entry_name,
        .size = rom.size,
        .crc32 = rom.hash_digest.to_hex_crc32(),
        .md5 = rom.hash_digest.to_hex_md5(),
        .sha1 = rom.hash_digest.to_hex_sha1(),
        .sha256 = rom.hash_digest.to_hex_sha256(),
        .last_write_time = rom.last_write_time,
    };
    auto upsert = db_->upsert_file(file);
    if (!upsert) {
      ROMULUS_WARN("DB upsert failed for '{}': {}", file.path, upsert.error().message);
    }
  }

  auto commit = txn->commit();
  if (!commit) {
    auto rollback = txn->rollback();
    if (!rollback) {
      return std::unexpected(
          core::Error{commit.error().code,
                      commit.error().message + "; rollback failed: " + rollback.error().message});
    }
    return std::unexpected(commit.error());
  }

  return result->report;
}

Result<void> RomulusService::verify(std::optional<std::string> dat_name) {
  // Step 1: Match files to ROMs
  auto matches = engine::Matcher::match_all(*db_);
  if (!matches) {
    return std::unexpected(matches.error());
  }

  // Step 2: Classify and log ROM statuses
  auto dat_id = resolve_optional_dat_id(dat_name);
  if (!dat_id) {
    return std::unexpected(dat_id.error());
  }

  return engine::Classifier::classify_all(*db_, *dat_id);
}

Result<void> RomulusService::full_sync(const std::filesystem::path& dat_path,
                                       const std::filesystem::path& rom_dir) {
  ROMULUS_INFO("Starting full sync: ROM dir={}, DAT={}", rom_dir.string(), dat_path.string());

  // 0. Fail-fast: validate the DAT path before spending time scanning.
  //    A missing or unreadable DAT file is a common user error; report it
  //    immediately rather than after a potentially lengthy scan.
  auto validated_dat = dat::DatFetcher::validate_local(dat_path);
  if (!validated_dat) {
    return std::unexpected(validated_dat.error());
  }

  // 1. Scan directory — scanning is independent of any DAT, so it runs first.
  //    Persisted scan results can then be reused by later DAT imports/verifications,
  //    while repeated scans rely on the scan cache to skip re-hashing unchanged files.
  auto scan = scan_directory(rom_dir);
  if (!scan) {
    return std::unexpected(scan.error());
  }

  // 2. Import DAT — load the expectations (what correct ROMs look like).
  auto dat = import_dat(dat_path);
  if (!dat) {
    return std::unexpected(dat.error());
  }

  // 3. Verify (match + classify) — link reality to expectations.
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

Result<core::CollectionSummary> RomulusService::get_summary(std::optional<std::string> dat_name) {
  auto dat_id = resolve_optional_dat_id(dat_name);
  if (!dat_id) {
    return std::unexpected(dat_id.error());
  }
  return db_->get_collection_summary(*dat_id);
}

Result<std::vector<core::DatVersion>> RomulusService::list_dat_versions() {
  return db_->get_all_dat_versions();
}

Result<std::vector<std::pair<core::RomInfo, core::RomStatusType>>> RomulusService::
    get_roms_with_status(std::int64_t dat_version_id) {
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

Result<std::vector<core::MissingRom>> RomulusService::get_missing_roms(
    std::optional<std::string> dat_name) {
  auto dat_id = resolve_optional_dat_id(dat_name);
  if (!dat_id) {
    return std::unexpected(dat_id.error());
  }
  return db_->get_missing_roms(*dat_id);
}

Result<std::vector<core::FileInfo>> RomulusService::get_all_files() {
  return db_->get_all_files();
}

// ═══════════════════════════════════════════════════════════════
// Admin
// ═══════════════════════════════════════════════════════════════

Result<void> RomulusService::purge_database() {
  //   rom_matches     →  roms, global_roms
  //   files           →  global_roms
  //   global_roms     (no remaining children)
  //   roms            →  games
  //   games           →  dat_versions
  //   dat_versions    (no remaining children)
  //   scanned_directories (independent — no FK children)
  static constexpr std::array k_DeleteQueries = {
      "DELETE FROM rom_matches",
      "DELETE FROM files",
      "DELETE FROM global_roms",
      "DELETE FROM roms",
      "DELETE FROM games",
      "DELETE FROM dat_versions",
      "DELETE FROM scanned_directories",
  };

  auto txn = db_->begin_transaction();
  if (!txn) {
    return std::unexpected(txn.error());
  }
  for (const auto* query : k_DeleteQueries) {
    auto result = db_->execute(query);
    if (!result) {
      return std::unexpected(result.error());
    }
  }
  auto commit = txn->commit();
  if (!commit) {
    return std::unexpected(commit.error());
  }

  // Force WAL checkpoint so the main .db file reflects the purge immediately.
  // Without this, data may still appear in the DB file until SQLite checkpoints.
  auto wal = db_->execute("PRAGMA wal_checkpoint(TRUNCATE)");
  if (!wal) {
    ROMULUS_WARN("WAL checkpoint after purge failed: {}", wal.error().message);
  }

  ROMULUS_INFO("Database purged — all tables cleared");
  return {};
}

Result<void> RomulusService::delete_dat(std::int64_t id) {
  auto result = db_->delete_dat_version(id);
  if (!result) {
    return result;
  }
  ROMULUS_INFO("Deleted DAT version id={}", id);
  return {};
}

// ═══════════════════════════════════════════════════════════════
// Scanned Directories
// ═══════════════════════════════════════════════════════════════

Result<core::ScannedDirectory> RomulusService::add_scan_directory(
    const std::filesystem::path& dir) {
  return db_->add_scanned_directory(dir.string());
}

Result<std::vector<core::ScannedDirectory>> RomulusService::get_scan_directories() {
  return db_->get_all_scanned_directories();
}

Result<void> RomulusService::remove_scan_directory(std::int64_t id) {
  return db_->remove_scanned_directory(id);
}

// ═══════════════════════════════════════════════════════════════
// Reports
// ═══════════════════════════════════════════════════════════════

Result<std::string> RomulusService::generate_report(core::ReportType type,
                                                    core::ReportFormat format,
                                                    std::optional<std::string> dat_name) {
  auto dat_id = resolve_optional_dat_id(dat_name);
  if (!dat_id) {
    return std::unexpected(dat_id.error());
  }
  return report::ReportGenerator::generate(*db_, type, format, *dat_id);
}

// ═══════════════════════════════════════════════════════════════
// DB Explorer
// ═══════════════════════════════════════════════════════════════

std::filesystem::path RomulusService::get_db_path() const {
  return db_path_;
}

Result<std::vector<std::string>> RomulusService::get_db_table_names() {
  return db_->get_table_names();
}

Result<core::TableQueryResult> RomulusService::query_db_table(std::string_view table_name) {
  return db_->query_table_data(table_name);
}

// ═══════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════

Result<std::optional<std::int64_t>> RomulusService::resolve_optional_dat_id(
    const std::optional<std::string>& dat_name) {
  if (!dat_name.has_value()) {
    return std::nullopt;
  }
  auto id_result = resolve_dat_version_id(*dat_name);
  if (!id_result) {
    return std::unexpected(id_result.error());
  }
  return *id_result;
}

Result<std::int64_t> RomulusService::resolve_dat_version_id(const std::string& dat_name) {
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
