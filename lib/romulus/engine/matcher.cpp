#include "romulus/engine/matcher.hpp"

#include "romulus/core/logging.hpp"
#include "romulus/database/database.hpp"

namespace romulus::engine {

auto Matcher::match_all(database::Database& db) -> Result<std::vector<core::MatchResult>> {
  // Clear previous matches
  auto clear_result = db.clear_matches();
  if (!clear_result) {
    return std::unexpected(clear_result.error());
  }

  auto files = db.get_all_files();
  if (!files) {
    return std::unexpected(files.error());
  }

  ROMULUS_INFO("Matching {} files against ROM database...", files->size());

  std::vector<core::MatchResult> results;
  auto txn = db.begin_transaction();

  for (const auto& file : *files) {
    core::MatchResult match{.file_id = file.id};

    // Priority 1: SHA1 match (strongest)
    if (!file.sha1.empty()) {
      auto rom = db.find_rom_by_sha1(file.sha1);
      if (rom && rom->has_value()) {
        // Check if MD5 and CRC32 also match for "exact"
        bool md5_match = file.md5 == rom->value().md5;
        bool crc_match = file.crc32 == rom->value().crc32;

        match.rom_id = rom->value().id;
        if (md5_match && crc_match) {
          match.match_type = core::MatchType::Exact;
        } else {
          match.match_type = core::MatchType::Sha1Only;
        }

        auto ins = db.insert_file_match(match);
        if (!ins) {
          ROMULUS_WARN("Failed to insert match: {}", ins.error().message);
        }
        results.push_back(match);
        continue;
      }
    }

    // Priority 2: MD5 match
    if (!file.md5.empty()) {
      auto rom = db.find_rom_by_md5(file.md5);
      if (rom && rom->has_value()) {
        match.rom_id = rom->value().id;
        match.match_type = core::MatchType::Md5Only;

        auto ins = db.insert_file_match(match);
        if (!ins) {
          ROMULUS_WARN("Failed to insert match: {}", ins.error().message);
        }
        results.push_back(match);
        continue;
      }
    }

    // Priority 3: CRC32 match (weakest, may have multiple)
    if (!file.crc32.empty()) {
      auto roms = db.find_rom_by_crc32(file.crc32);
      if (roms && !roms->empty()) {
        // Take the first CRC32 match
        match.rom_id = roms->front().id;
        match.match_type = core::MatchType::Crc32Only;

        auto ins = db.insert_file_match(match);
        if (!ins) {
          ROMULUS_WARN("Failed to insert match: {}", ins.error().message);
        }
        results.push_back(match);
        continue;
      }
    }

    // No match
    match.match_type = core::MatchType::NoMatch;
    results.push_back(match);
  }

  txn.commit();

  auto matched = std::ranges::count_if(results, [](const auto& r) {
    return r.match_type != core::MatchType::NoMatch;
  });

  ROMULUS_INFO(
    "Matching complete: {} matched, {} unmatched",
    matched, results.size() - static_cast<std::size_t>(matched));

  return results;
}

} // namespace romulus::engine
