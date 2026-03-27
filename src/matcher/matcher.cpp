#include "romulus/matcher/matcher.hpp"

#include <spdlog/spdlog.h>

#include <ranges>
#include <unordered_map>

namespace romulus {

Matcher::Matcher(Database& db) : db_(&db) {}

std::expected<std::vector<FileMatch>, Error> Matcher::match(
    std::vector<ScannedFile> files) const {
  auto roms_result = db_->query_all_roms();
  if (!roms_result) {
    spdlog::error("Matcher::match: failed to query ROMs");
    return std::unexpected(roms_result.error());
  }

  const auto& roms = *roms_result;

  // Build lookup maps for fast matching
  // sha1+name -> rom
  std::unordered_map<std::string, const Rom*> exact_map;
  // crc32 -> rom
  std::unordered_map<std::string, const Rom*> crc_map;
  // sha1 -> rom (for renamed detection)
  std::unordered_map<std::string, const Rom*> sha1_map;

  for (const auto& rom : roms) {
    if (!rom.sha1.empty() && !rom.name.empty()) {
      exact_map[rom.sha1 + "|" + rom.name] = &rom;
    }
    if (!rom.crc32.empty()) {
      crc_map.try_emplace(rom.crc32, &rom);
    }
    if (!rom.sha1.empty()) {
      sha1_map.try_emplace(rom.sha1, &rom);
    }
  }

  std::vector<FileMatch> matches;
  matches.reserve(files.size());

  for (const auto& sf : files) {
    const std::string filename = sf.path.filename().string();

    // Try exact match: sha1 + filename
    if (!sf.sha1.empty()) {
      const std::string exact_key = sf.sha1 + "|" + filename;
      if (auto it = exact_map.find(exact_key); it != exact_map.end()) {
        FileMatch fm;
        fm.file_id = sf.id;
        fm.rom_id = it->second->id;
        fm.match_type = MatchType::Exact;
        matches.push_back(fm);
        continue;
      }
    }

    // Try CRC-only match
    if (!sf.crc32.empty()) {
      if (auto it = crc_map.find(sf.crc32); it != crc_map.end()) {
        FileMatch fm;
        fm.file_id = sf.id;
        fm.rom_id = it->second->id;
        fm.match_type = MatchType::CrcOnly;
        matches.push_back(fm);
        continue;
      }
    }

    // Try renamed match: sha1 matches but name differs
    if (!sf.sha1.empty()) {
      if (auto it = sha1_map.find(sf.sha1); it != sha1_map.end()) {
        FileMatch fm;
        fm.file_id = sf.id;
        fm.rom_id = it->second->id;
        fm.match_type = MatchType::Renamed;
        matches.push_back(fm);
        continue;
      }
    }
  }

  spdlog::info("Matcher::match: matched {} / {} files", matches.size(), files.size());
  return matches;
}

}  // namespace romulus
