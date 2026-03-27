#include "romulus/report_generator/report_generator.hpp"

#include <spdlog/spdlog.h>

#include <map>
#include <sstream>
#include <unordered_map>

namespace romulus {

ReportGenerator::ReportGenerator(Database& db) : db_(&db) {}

std::expected<std::string, Error> ReportGenerator::generate_text_report() const {
  auto roms_result = db_->query_all_roms();
  if (!roms_result) {
    spdlog::error("ReportGenerator: failed to query ROMs");
    return std::unexpected(roms_result.error());
  }

  auto statuses_result = db_->query_rom_statuses();
  if (!statuses_result) {
    spdlog::error("ReportGenerator: failed to query ROM statuses");
    return std::unexpected(statuses_result.error());
  }

  auto games_result = db_->query_all_games();
  if (!games_result) {
    spdlog::error("ReportGenerator: failed to query games");
    return std::unexpected(games_result.error());
  }

  // Build game_id -> system name map for proper system grouping
  std::unordered_map<Id, std::string> game_system_map;
  for (const auto& game : *games_result) {
    game_system_map[game.id] = game.system;
  }

  // Build ROM status lookup
  std::unordered_map<Id, RomStatus> status_map;
  for (const auto& rs : *statuses_result) {
    status_map[rs.rom_id] = rs.status;
  }

  // Per-system stats: system name -> {have, missing, duplicate, bad_dump}
  struct Stats {
    std::size_t have{};
    std::size_t missing{};
    std::size_t duplicate{};
    std::size_t bad_dump{};

    [[nodiscard]] std::size_t total() const noexcept {
      return have + missing + duplicate + bad_dump;
    }
  };

  std::map<std::string, Stats> system_stats;

  for (const auto& rom : *roms_result) {
    const auto system_it = game_system_map.find(rom.game_id);
    const std::string system_name =
        (system_it != game_system_map.end()) ? system_it->second : "Unknown";

    const auto status_it = status_map.find(rom.id);
    const RomStatus status =
        (status_it != status_map.end()) ? status_it->second : RomStatus::Missing;

    auto& stats = system_stats[system_name];
    switch (status) {
      case RomStatus::Have:
        stats.have++;
        break;
      case RomStatus::Missing:
        stats.missing++;
        break;
      case RomStatus::Duplicate:
        stats.duplicate++;
        break;
      case RomStatus::BadDump:
        stats.bad_dump++;
        break;
    }
  }

  std::ostringstream oss;
  oss << "========================================\n";
  oss << "  ROMULUS - ROM Collection Report\n";
  oss << "========================================\n\n";

  if (system_stats.empty()) {
    oss << "  No data available. Run a scan and classification first.\n";
  } else {
    for (const auto& [system, stats] : system_stats) {
      const double pct =
          stats.total() > 0U
              ? (static_cast<double>(stats.have) / static_cast<double>(stats.total())) *
                    100.0
              : 0.0;

      oss << "System: " << system << "\n";
      oss << "  Total  : " << stats.total() << "\n";
      oss << "  Have   : " << stats.have << "\n";
      oss << "  Missing: " << stats.missing << "\n";
      oss << "  Dup    : " << stats.duplicate << "\n";
      oss << "  BadDump: " << stats.bad_dump << "\n";
      char pct_buf[16];
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
      std::snprintf(pct_buf, sizeof(pct_buf), "%.1f", pct);
      oss << "  Complete: " << pct_buf << "%\n\n";
    }
  }

  oss << "========================================\n";

  spdlog::info("ReportGenerator: report generated ({} systems)", system_stats.size());
  return oss.str();
}

}  // namespace romulus
