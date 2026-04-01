#include "romulus/service/romulus_service.hpp"

#include "romulus/core/logging.hpp"
#include "romulus/dat/dat_fetcher.hpp"
#include "romulus/dat/dat_parser.hpp"
#include "romulus/database/database.hpp"
#include "romulus/engine/classifier.hpp"
#include "romulus/engine/matcher.hpp"
#include "romulus/report/report_generator.hpp"
#include "romulus/scanner/rom_scanner.hpp"

#include <utility>

namespace romulus::service {

RomulusService::RomulusService(const std::filesystem::path& db_path)
    : db_(std::make_unique<database::Database>(db_path)) {
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

  // Parse DAT
  dat::DatParser parser;
  auto dat_file = parser.parse(*validated);
  if (!dat_file) {
    return std::unexpected(dat_file.error());
  }

  // Check if already imported
  auto existing = db_->find_dat_version(dat_file->header.name, dat_file->header.version);
  if (existing && existing->has_value()) {
    ROMULUS_INFO("DAT already imported: '{}' v{}", dat_file->header.name, dat_file->header.version);
    return existing->value();
  }

  // Get or create system
  auto system_id = db_->get_or_create_system(dat_file->header.name);
  if (!system_id) {
    return std::unexpected(system_id.error());
  }

  // Store DAT version
  core::DatVersion dat_version{
      .system_id = *system_id,
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

  // Store games and ROMs in a transaction
  auto txn = db_->begin_transaction();

  for (const auto& game : dat_file->games) {
    core::GameInfo game_entry{
        .name = game.name,
        .description = {},
        .system_id = *system_id,
        .dat_version_id = *dat_id,
        .roms = {},
    };

    auto game_id = db_->insert_game(game_entry);
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
                                    std::optional<std::string> extensions)
    -> Result<core::ScanReport> {
  return scanner::RomScanner::scan(dir, *db_, std::move(extensions));
}

auto RomulusService::verify(std::optional<std::string> system) -> Result<void> {
  auto sys_id = resolve_system_id(system);
  if (!sys_id) {
    return std::unexpected(sys_id.error());
  }

  // Step 1: Match files to ROMs
  auto matches = engine::Matcher::match_all(*db_);
  if (!matches) {
    return std::unexpected(matches.error());
  }

  // Step 2: Classify ROM statuses
  return engine::Classifier::classify_all(*db_, *sys_id);
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

auto RomulusService::get_summary(std::optional<std::string> system)
    -> Result<core::CollectionSummary> {
  auto sys_id = resolve_system_id(system);
  if (!sys_id) {
    return std::unexpected(sys_id.error());
  }
  return db_->get_collection_summary(*sys_id);
}

auto RomulusService::list_systems() -> Result<std::vector<core::SystemInfo>> {
  return db_->get_all_systems();
}

auto RomulusService::get_missing_roms(std::optional<std::string> system)
    -> Result<std::vector<core::MissingRom>> {
  auto sys_id = resolve_system_id(system);
  if (!sys_id) {
    return std::unexpected(sys_id.error());
  }
  return db_->get_missing_roms(*sys_id);
}

auto RomulusService::get_all_files() -> Result<std::vector<core::FileInfo>> {
  return db_->get_all_files();
}

// ═══════════════════════════════════════════════════════════════
// Admin
// ═══════════════════════════════════════════════════════════════

auto RomulusService::execute_raw(const std::string& sql) -> Result<void> {
  return db_->execute(sql);
}

// ═══════════════════════════════════════════════════════════════
// Reports
// ═══════════════════════════════════════════════════════════════

auto RomulusService::generate_report(core::ReportType type,
                                     core::ReportFormat format,
                                     std::optional<std::string> system) -> Result<std::string> {
  auto sys_id = resolve_system_id(system);
  if (!sys_id) {
    return std::unexpected(sys_id.error());
  }
  return report::ReportGenerator::generate(*db_, type, format, *sys_id);
}

// ═══════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════

auto RomulusService::resolve_system_id(const std::optional<std::string>& system)
    -> Result<std::optional<std::int64_t>> {
  if (!system.has_value()) {
    return std::nullopt;
  }

  auto sys = db_->find_system_by_name(*system);
  if (!sys) {
    return std::unexpected(sys.error());
  }
  if (!sys->has_value()) {
    return std::unexpected(
        core::Error{core::ErrorCode::NotFound, "System not found: '" + *system + "'"});
  }

  return sys->value().id;
}

} // namespace romulus::service
