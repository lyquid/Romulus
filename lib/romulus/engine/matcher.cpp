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

  auto roms = db.get_all_roms();
  if (!roms) {
    return std::unexpected(roms.error());
  }

  ROMULUS_INFO("Matching ROMs against Global Index (Priority: SHA1 > MD5 > CRC32 > SHA256)...");

  std::vector<core::MatchResult> results;
  auto txn = db.begin_transaction();
  if (!txn) {
    return std::unexpected(txn.error());
  }

  for (const auto& rom : *roms) {
    core::MatchResult match{
        .rom_id = rom.id, .global_rom_sha1 = "", .match_type = core::MatchType::NoMatch};

    // Priority 1: SHA1 match (standard for No-Intro, Redump, etc.)
    // Uses the global_roms.sha1 PRIMARY KEY index — O(log M) lookup.
    if (!rom.sha1.empty()) {
      auto global = db.find_global_rom_by_sha1(rom.sha1);
      if (!global) {
        return std::unexpected(global.error());
      }
      if (global->has_value()) {
        const auto& g_rom = global->value();
        bool md5_match = rom.md5.empty() || g_rom.md5.empty() || rom.md5 == g_rom.md5;
        bool crc_match = rom.crc32.empty() || g_rom.crc32.empty() || rom.crc32 == g_rom.crc32;

        match.global_rom_sha1 = g_rom.sha1;
        match.match_type =
            (md5_match && crc_match) ? core::MatchType::Exact : core::MatchType::Sha1Only;

        auto ins = db.insert_rom_match(match);
        if (!ins) {
          ROMULUS_WARN("Failed to insert match: {}", ins.error().message);
        }
        results.push_back(match);
        continue;
      }
    }

    // Priority 2: MD5 match
    // Uses the idx_global_roms_md5 index — O(log M) lookup.
    if (!rom.md5.empty()) {
      auto global = db.find_global_rom_by_md5(rom.md5);
      if (!global) {
        return std::unexpected(global.error());
      }
      if (global->has_value()) {
        match.global_rom_sha1 = global->value().sha1;
        match.match_type = core::MatchType::Md5Only;

        auto ins = db.insert_rom_match(match);
        if (!ins) {
          ROMULUS_WARN("Failed to insert match: {}", ins.error().message);
        }
        results.push_back(match);
        continue;
      }
    }

    // Priority 3: CRC32 match
    // Uses the idx_global_roms_crc32 index — O(log M) lookup.
    if (!rom.crc32.empty()) {
      auto globals = db.find_global_rom_by_crc32(rom.crc32);
      if (!globals) {
        return std::unexpected(globals.error());
      }
      if (!globals->empty()) {
        match.global_rom_sha1 = globals->front().sha1;
        match.match_type = core::MatchType::Crc32Only;

        auto ins = db.insert_rom_match(match);
        if (!ins) {
          ROMULUS_WARN("Failed to insert match: {}", ins.error().message);
        }
        results.push_back(match);
        continue;
      }
    }

    // Priority 4: SHA256 match (bonus enrichment — not standard in the DAT ecosystem)
    // Uses the idx_global_roms_sha256 index — O(log M) lookup.
    if (!rom.sha256.empty()) {
      auto global = db.find_global_rom_by_sha256(rom.sha256);
      if (!global) {
        return std::unexpected(global.error());
      }
      if (global->has_value()) {
        match.global_rom_sha1 = global->value().sha1;
        match.match_type = core::MatchType::Sha256Only;

        auto ins = db.insert_rom_match(match);
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

  auto commit = txn->commit();
  if (!commit) {
    return std::unexpected(commit.error());
  }

  auto matched = std::count_if(results.begin(), results.end(), [](const auto& r) {
    return r.match_type != core::MatchType::NoMatch;
  });

  ROMULUS_INFO("Matching complete: {} matched, {} unmatched",
               matched,
               results.size() - static_cast<std::size_t>(matched));

  return results;
}

} // namespace romulus::engine
