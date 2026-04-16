#include "romulus/engine/matcher.hpp"

#include "romulus/core/logging.hpp"
#include "romulus/database/database.hpp"

#include <algorithm>
#include <string_view>
#include <unordered_map>

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

  auto global_roms = db.get_all_global_roms();
  if (!global_roms) {
    return std::unexpected(global_roms.error());
  }

  ROMULUS_INFO("Matching ROMs against Global Index (Priority: SHA1 > SHA256 > MD5 > CRC32)...");

  std::
      unordered_map<std::string_view, const core::GlobalRom*, core::StringViewHash, std::equal_to<>>
          global_rom_by_sha1;
  std::
      unordered_map<std::string_view, const core::GlobalRom*, core::StringViewHash, std::equal_to<>>
          global_rom_by_sha256;
  std::
      unordered_map<std::string_view, const core::GlobalRom*, core::StringViewHash, std::equal_to<>>
          global_rom_by_md5;
  std::unordered_map<std::string_view,
                     std::vector<const core::GlobalRom*>,
                     core::StringViewHash,
                     std::equal_to<>>
      global_roms_by_crc32;

  global_rom_by_sha1.reserve(global_roms->size());
  global_rom_by_sha256.reserve(global_roms->size());
  global_rom_by_md5.reserve(global_roms->size());
  global_roms_by_crc32.reserve(global_roms->size());

  for (const auto& global_rom : *global_roms) {
    if (!global_rom.sha1.empty()) {
      global_rom_by_sha1.try_emplace(global_rom.sha1, &global_rom);
    }
    if (!global_rom.sha256.empty()) {
      global_rom_by_sha256.try_emplace(global_rom.sha256, &global_rom);
    }
    if (!global_rom.md5.empty()) {
      global_rom_by_md5.try_emplace(global_rom.md5, &global_rom);
    }
    if (!global_rom.crc32.empty()) {
      global_roms_by_crc32[global_rom.crc32].push_back(&global_rom);
    }
  }

  std::vector<core::MatchResult> results;
  auto txn = db.begin_transaction();
  if (!txn) {
    return std::unexpected(txn.error());
  }

  for (const auto& rom : *roms) {
    core::MatchResult match{
        .rom_id = rom.id, .global_rom_sha1 = "", .match_type = core::MatchType::NoMatch};

    // Priority 1: SHA1 match (Standard for No-Intro, Redump, etc.)
    if (!rom.sha1.empty()) {
      const auto sha1_it = global_rom_by_sha1.find(rom.sha1);
      if (sha1_it != global_rom_by_sha1.end()) {
        const auto& g_rom = *sha1_it->second;
        bool md5_match = rom.md5.empty() || g_rom.md5.empty() || rom.md5 == g_rom.md5;
        bool crc_match = rom.crc32.empty() || g_rom.crc32.empty() || rom.crc32 == g_rom.crc32;

        match.global_rom_sha1 = g_rom.sha1;
        if (md5_match && crc_match) {
          match.match_type = core::MatchType::Exact;
        } else {
          match.match_type = core::MatchType::Sha1Only;
        }

        auto ins = db.insert_rom_match(match);
        if (!ins) {
          ROMULUS_WARN("Failed to insert match: {}", ins.error().message);
        }
        results.push_back(match);
        continue;
      }
    }

    // Priority 2: SHA256 match (Optional / fallback if DAT specifically supports it and SHA1
    // didn't exist or wasn't provided)
    if (!rom.sha256.empty()) {
      const auto sha256_it = global_rom_by_sha256.find(rom.sha256);
      if (sha256_it != global_rom_by_sha256.end()) {
        match.global_rom_sha1 = sha256_it->second->sha1;
        match.match_type = core::MatchType::Sha256Only;

        auto ins = db.insert_rom_match(match);
        if (!ins) {
          ROMULUS_WARN("Failed to insert match: {}", ins.error().message);
        }
        results.push_back(match);
        continue;
      }
    }

    // Priority 3: MD5 match
    if (!rom.md5.empty()) {
      const auto md5_it = global_rom_by_md5.find(rom.md5);
      if (md5_it != global_rom_by_md5.end()) {
        match.global_rom_sha1 = md5_it->second->sha1;
        match.match_type = core::MatchType::Md5Only;

        auto ins = db.insert_rom_match(match);
        if (!ins) {
          ROMULUS_WARN("Failed to insert match: {}", ins.error().message);
        }
        results.push_back(match);
        continue;
      }
    }

    // Priority 4: CRC32 match (weakest)
    if (!rom.crc32.empty()) {
      const auto crc32_it = global_roms_by_crc32.find(rom.crc32);
      if (crc32_it != global_roms_by_crc32.end() && !crc32_it->second.empty()) {
        // Take the first match
        match.global_rom_sha1 = crc32_it->second.front()->sha1;
        match.match_type = core::MatchType::Crc32Only;

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
