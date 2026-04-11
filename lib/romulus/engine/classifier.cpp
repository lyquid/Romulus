#include "romulus/engine/classifier.hpp"

#include "romulus/core/logging.hpp"
#include "romulus/database/database.hpp"

namespace romulus::engine {

auto Classifier::classify_all(database::Database& db,
                              std::optional<std::int64_t> dat_version_id) -> Result<void> {
  std::vector<core::RomInfo> roms_to_process;

  if (dat_version_id.has_value()) {
    auto roms = db.get_roms_for_dat_version(*dat_version_id);
    if (!roms) {
      return std::unexpected(roms.error());
    }
    roms_to_process = std::move(*roms);
  } else {
    auto roms = db.get_all_roms();
    if (!roms) {
      return std::unexpected(roms.error());
    }
    roms_to_process = std::move(*roms);
  }

  ROMULUS_INFO("Classifying {} ROM(s)...", roms_to_process.size());

  std::int64_t verified = 0;
  std::int64_t missing = 0;
  std::int64_t unverified = 0;
  std::int64_t mismatch = 0;

  for (const auto& rom : roms_to_process) {
    auto status = db.get_computed_rom_status(rom.id);
    if (!status) {
      ROMULUS_WARN("Failed to compute status for ROM '{}': {}", rom.name, status.error().message);
      continue;
    }

    switch (*status) {
      case core::RomStatusType::Verified:
        ++verified;
        break;
      case core::RomStatusType::Missing:
        ++missing;
        break;
      case core::RomStatusType::Unverified:
        ++unverified;
        break;
      case core::RomStatusType::Mismatch:
        ++mismatch;
        break;
    }
  }

  ROMULUS_INFO("Classification complete: {} verified, {} missing, {} unverified, {} mismatch",
               verified,
               missing,
               unverified,
               mismatch);

  return {};
}

} // namespace romulus::engine
