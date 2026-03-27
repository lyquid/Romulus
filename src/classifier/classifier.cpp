#include "romulus/classifier/classifier.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <format>
#include <unordered_map>
#include <unordered_set>

namespace romulus {

Classifier::Classifier(Database& db) : db_(&db) {}

std::expected<std::vector<RomStatusRecord>, Error> Classifier::classify() const {
  auto roms_result = db_->query_all_roms();
  if (!roms_result) {
    spdlog::error("Classifier::classify: failed to query ROMs");
    return std::unexpected(roms_result.error());
  }

  auto matches_result = db_->query_file_matches();
  if (!matches_result) {
    spdlog::error("Classifier::classify: failed to query file matches");
    return std::unexpected(matches_result.error());
  }

  auto files_result = db_->query_scanned_files();
  if (!files_result) {
    spdlog::error("Classifier::classify: failed to query scanned files");
    return std::unexpected(files_result.error());
  }

  // Count how many files match each ROM
  std::unordered_map<Id, std::size_t> rom_match_count;
  // Track match types per ROM (to detect BadDump = CrcOnly with no exact)
  std::unordered_map<Id, MatchType> rom_best_match;

  for (const auto& fm : *matches_result) {
    rom_match_count[fm.rom_id]++;
    auto it = rom_best_match.find(fm.rom_id);
    if (it == rom_best_match.end()) {
      rom_best_match[fm.rom_id] = fm.match_type;
    } else {
      // Prefer higher-quality match type (Exact > Renamed > CrcOnly)
      if (fm.match_type == MatchType::Exact) {
        it->second = MatchType::Exact;
      } else if (fm.match_type == MatchType::Renamed &&
                 it->second == MatchType::CrcOnly) {
        it->second = MatchType::Renamed;
      }
    }
  }

  const auto now = std::chrono::system_clock::now();
  const auto ts = std::format("{:%Y-%m-%dT%H:%M:%SZ}", now);

  std::vector<RomStatusRecord> statuses;
  statuses.reserve(roms_result->size());

  for (const auto& rom : *roms_result) {
    RomStatusRecord rs;
    rs.rom_id = rom.id;
    rs.last_updated = ts;

    const auto count_it = rom_match_count.find(rom.id);
    if (count_it == rom_match_count.end()) {
      // No matches at all
      rs.status = RomStatus::Missing;
    } else {
      const std::size_t match_count = count_it->second;
      const MatchType best = rom_best_match.at(rom.id);

      if (match_count > 1U) {
        rs.status = RomStatus::Duplicate;
      } else if (best == MatchType::CrcOnly) {
        // CRC matches but SHA1 does not — potential bad dump
        rs.status = RomStatus::BadDump;
      } else {
        rs.status = RomStatus::Have;
      }
    }

    statuses.push_back(rs);
  }

  spdlog::info("Classifier::classify: classified {} ROMs", statuses.size());
  return statuses;
}

}  // namespace romulus
