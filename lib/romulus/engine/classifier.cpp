#include "romulus/engine/classifier.hpp"

#include "romulus/core/logging.hpp"
#include "romulus/database/database.hpp"

namespace romulus::engine {

auto Classifier::classify_all(database::Database& db,
                              std::optional<std::int64_t> dat_version_id) -> Result<void> {
  ROMULUS_INFO("Computing collection summary...");

  auto summary = db.get_collection_summary(dat_version_id);
  if (!summary) {
    return std::unexpected(summary.error());
  }

  ROMULUS_INFO("Collection summary computed for {} ROM(s).", summary->total_roms);
  ROMULUS_INFO("Classification complete: {} verified, {} missing, {} unverified, {} mismatch",
               summary->verified,
               summary->missing,
               summary->unverified,
               summary->mismatch);

  return {};
}

} // namespace romulus::engine
