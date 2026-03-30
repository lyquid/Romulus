#include "romulus/engine/classifier.hpp"

#include "romulus/core/logging.hpp"
#include "romulus/database/database.hpp"

namespace romulus::engine {

auto Classifier::classify_all(database::Database& db,
                              std::optional<std::int64_t> system_id) -> Result<void> {
  // Get all systems to process
  std::vector<core::SystemInfo> systems_to_process;

  if (system_id.has_value()) {
    auto all_systems = db.get_all_systems();
    if (!all_systems) {
      return std::unexpected(all_systems.error());
    }
    for (const auto& sys : *all_systems) {
      if (sys.id == *system_id) {
        systems_to_process.push_back(sys);
        break;
      }
    }
  } else {
    auto all_systems = db.get_all_systems();
    if (!all_systems) {
      return std::unexpected(all_systems.error());
    }
    systems_to_process = std::move(*all_systems);
  }

  ROMULUS_INFO("Classifying ROMs for {} system(s)...", systems_to_process.size());

  auto txn = db.begin_transaction();
  std::int64_t verified = 0;
  std::int64_t missing = 0;
  std::int64_t unverified = 0;
  std::int64_t mismatch = 0;

  for (const auto& sys : systems_to_process) {
    auto roms = db.get_all_roms_for_system(sys.id);
    if (!roms) {
      ROMULUS_WARN("Failed to get ROMs for system '{}': {}", sys.name, roms.error().message);
      continue;
    }

    for (const auto& rom : *roms) {
      auto matches = db.get_matches_for_rom(rom.id);
      if (!matches) {
        continue;
      }

      core::RomStatusType status;

      if (matches->empty()) {
        status = core::RomStatusType::Missing;
        ++missing;
      } else {
        // Check best match type
        bool has_exact = false;
        bool has_partial = false;
        for (const auto& m : *matches) {
          if (m.match_type == core::MatchType::Exact) {
            has_exact = true;
          } else if (m.match_type != core::MatchType::NoMatch) {
            has_partial = true;
          }
        }

        if (has_exact) {
          status = core::RomStatusType::Verified;
          ++verified;
        } else if (has_partial) {
          status = core::RomStatusType::Unverified;
          ++unverified;
        } else {
          status = core::RomStatusType::Mismatch;
          ++mismatch;
        }
      }

      auto result = db.upsert_rom_status(rom.id, status);
      if (!result) {
        ROMULUS_WARN("Failed to update status for ROM '{}': {}", rom.name, result.error().message);
      }
    }
  }

  txn.commit();

  ROMULUS_INFO("Classification complete: {} verified, {} missing, {} unverified, {} mismatch",
               verified,
               missing,
               unverified,
               mismatch);

  return {};
}

} // namespace romulus::engine
