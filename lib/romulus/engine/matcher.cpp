#include "romulus/engine/matcher.hpp"

#include "romulus/core/logging.hpp"
#include "romulus/database/database.hpp"

#include <algorithm>

namespace romulus::engine {

auto Matcher::match_all(database::Database& db) -> Result<std::vector<core::MatchResult>> {
  // Clear previous matches
  auto clear_result = db.clear_matches();
  if (!clear_result) {
    return std::unexpected(clear_result.error());
  }

  auto systems = db.get_all_systems();
  if (!systems) {
    return std::unexpected(systems.error());
  }

  ROMULUS_INFO("Matching ROMs against Global Index (Priority: SHA1 > SHA256 > MD5 > CRC32)...");

  std::vector<core::MatchResult> results;
  auto txn = db.begin_transaction();

  for (const auto& sys : *systems) {
    auto roms = db.get_all_roms_for_system(sys.id);
    if (!roms) {
      continue;
    }

    for (const auto& rom : *roms) {
      core::MatchResult match{.rom_id = rom.id, .global_rom_sha1 = "", .match_type = core::MatchType::NoMatch};

      // Priority 1: SHA1 match (Standard for No-Intro, Redump, etc.)
      if (!rom.sha1.empty()) {
        auto g_rom = db.find_global_rom_by_sha1(rom.sha1);
        if (g_rom && g_rom->has_value()) {
          bool md5_match = rom.md5.empty() || g_rom->value().md5.empty() || rom.md5 == g_rom->value().md5;
          bool crc_match = rom.crc32.empty() || g_rom->value().crc32.empty() || rom.crc32 == g_rom->value().crc32;

          match.global_rom_sha1 = g_rom->value().sha1;
          if (md5_match && crc_match) {
            match.match_type = core::MatchType::Exact;
          } else {
            match.match_type = core::MatchType::Sha1Only;
          }

          auto ins = db.insert_rom_match(match);
          if (!ins) ROMULUS_WARN("Failed to insert match: {}", ins.error().message);
          results.push_back(match);
          continue;
        }
      }

      // Priority 2: SHA256 match (Optional / fallback if DAT specifically supports it and SHA1 didn't exist or wasn't provided)
      if (!rom.sha256.empty()) {
        auto g_rom = db.find_global_rom_by_sha256(rom.sha256);
        if (g_rom && g_rom->has_value()) {
          match.global_rom_sha1 = g_rom->value().sha1;
          match.match_type = core::MatchType::Sha256Only;
          
          auto ins = db.insert_rom_match(match);
          if (!ins) ROMULUS_WARN("Failed to insert match: {}", ins.error().message);
          results.push_back(match);
          continue;
        }
      }

      // Priority 3: MD5 match
      if (!rom.md5.empty()) {
        auto g_rom = db.find_global_rom_by_md5(rom.md5);
        if (g_rom && g_rom->has_value()) {
          match.global_rom_sha1 = g_rom->value().sha1;
          match.match_type = core::MatchType::Md5Only;

          auto ins = db.insert_rom_match(match);
          if (!ins) ROMULUS_WARN("Failed to insert match: {}", ins.error().message);
          results.push_back(match);
          continue;
        }
      }

      // Priority 4: CRC32 match (weakest)
      if (!rom.crc32.empty()) {
        auto g_roms = db.find_global_rom_by_crc32(rom.crc32);
        if (g_roms && !g_roms->empty()) {
          // Take the first match
          match.global_rom_sha1 = g_roms->front().sha1;
          match.match_type = core::MatchType::Crc32Only;

          auto ins = db.insert_rom_match(match);
          if (!ins) ROMULUS_WARN("Failed to insert match: {}", ins.error().message);
          results.push_back(match);
          continue;
        }
      }

      // No match
      match.match_type = core::MatchType::NoMatch;
      results.push_back(match);
    }
  }

  txn.commit();

  auto matched = std::count_if(results.begin(), results.end(), [](const auto& r) {
    return r.match_type != core::MatchType::NoMatch;
  });

  ROMULUS_INFO("Matching complete: {} matched, {} unmatched",
               matched,
               results.size() - static_cast<std::size_t>(matched));

  return results;
}

} // namespace romulus::engine
