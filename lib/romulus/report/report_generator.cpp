#include "romulus/report/report_generator.hpp"

#include "romulus/database/database.hpp"

#include <nlohmann/json.hpp>

#include <iomanip>
#include <sstream>

namespace romulus::report {

namespace {

/// Escapes a field for CSV output per RFC 4180.
/// Wraps in double quotes if the value contains commas, quotes, or newlines.
std::string csv_escape(const std::string& field) {
  if (field.find_first_of(",\"\n\r") == std::string::npos) {
    return field;
  }
  std::string escaped = "\"";
  for (char c : field) {
    if (c == '"') {
      escaped += "\"\"";
    } else {
      escaped += c;
    }
  }
  escaped += '"';
  return escaped;
}

} // namespace

Result<std::string> ReportGenerator::generate(database::Database& db,
                                              core::ReportType type,
                                              core::ReportFormat format,
                                              std::optional<std::int64_t> system_id) {
  switch (type) {
    case core::ReportType::Summary:
      switch (format) {
        case core::ReportFormat::Text:
          return summary_text(db, system_id);
        case core::ReportFormat::Csv:
          return summary_csv(db, system_id);
        case core::ReportFormat::Json:
          return summary_json(db, system_id);
      }
      break;
    case core::ReportType::Missing:
      switch (format) {
        case core::ReportFormat::Text:
          return missing_text(db, system_id);
        case core::ReportFormat::Csv:
          return missing_csv(db, system_id);
        case core::ReportFormat::Json:
          return missing_json(db, system_id);
      }
      break;
    case core::ReportType::Duplicates:
      switch (format) {
        case core::ReportFormat::Text:
          return duplicates_text(db, system_id);
        case core::ReportFormat::Csv:
          return duplicates_csv(db, system_id);
        case core::ReportFormat::Json:
          return duplicates_json(db, system_id);
      }
      break;
    case core::ReportType::Unverified:
      switch (format) {
        case core::ReportFormat::Text:
          return unverified_text(db, system_id);
        case core::ReportFormat::Csv:
          return unverified_csv(db, system_id);
        case core::ReportFormat::Json:
          return unverified_json(db, system_id);
      }
      break;
  }

  return std::unexpected(
      core::Error{core::ErrorCode::InvalidArgument, "Invalid report type/format combination"});
}

// ═══════════════════════════════════════════════════════════════
// Summary Reports
// ═══════════════════════════════════════════════════════════════

Result<std::string> ReportGenerator::summary_text(database::Database& db,
                                                  std::optional<std::int64_t> sys) {
  auto summary = db.get_collection_summary(sys);
  if (!summary) {
    return std::unexpected(summary.error());
  }

  std::ostringstream out;
  out << "\n";
  out << "╔══════════════════════════════════════════════════╗\n";
  out << "║           ROMULUS — Collection Summary           ║\n";
  out << "╠══════════════════════════════════════════════════╣\n";
  out << "║ DAT:        " << std::setw(36) << std::left << summary->dat_name << " ║\n";
  out << "╠══════════════════════════════════════════════════╣\n";
  out << "║ Total ROMs: " << std::setw(36) << summary->total_roms << " ║\n";
  out << "║ Verified:   " << std::setw(36)
      << std::to_string(summary->verified) + " (" +
             std::to_string(static_cast<int>(summary->verified_percent())) + "%)"
      << " ║\n";
  out << "║ Missing:    " << std::setw(36) << summary->missing << " ║\n";
  out << "║ Unverified: " << std::setw(36) << summary->unverified << " ║\n";
  out << "║ Mismatch:   " << std::setw(36) << summary->mismatch << " ║\n";
  out << "╚══════════════════════════════════════════════════╝\n";

  return out.str();
}

Result<std::string> ReportGenerator::summary_csv(database::Database& db,
                                                 std::optional<std::int64_t> sys) {
  auto summary = db.get_collection_summary(sys);
  if (!summary) {
    return std::unexpected(summary.error());
  }

  std::ostringstream out;
  out << "dat,total_roms,verified,missing,unverified,mismatch,verified_pct\n";
  out << csv_escape(summary->dat_name) << "," << summary->total_roms << "," << summary->verified
      << "," << summary->missing << "," << summary->unverified << "," << summary->mismatch << ","
      << std::fixed << std::setprecision(1) << summary->verified_percent() << "\n";

  return out.str();
}

Result<std::string> ReportGenerator::summary_json(database::Database& db,
                                                  std::optional<std::int64_t> sys) {
  auto summary = db.get_collection_summary(sys);
  if (!summary) {
    return std::unexpected(summary.error());
  }

  nlohmann::json j;
  j["dat"] = summary->dat_name;
  j["total_roms"] = summary->total_roms;
  j["verified"] = summary->verified;
  j["missing"] = summary->missing;
  j["unverified"] = summary->unverified;
  j["mismatch"] = summary->mismatch;
  j["verified_percent"] = summary->verified_percent();

  return j.dump(2);
}

// ═══════════════════════════════════════════════════════════════
// Missing ROM Reports
// ═══════════════════════════════════════════════════════════════

Result<std::string> ReportGenerator::missing_text(database::Database& db,
                                                  std::optional<std::int64_t> sys) {
  auto missing = db.get_missing_roms(sys);
  if (!missing) {
    return std::unexpected(missing.error());
  }

  std::ostringstream out;
  out << "\n═══ Missing ROMs (" << missing->size() << ") ═══\n\n";

  std::string current_dat;
  for (const auto& rom : *missing) {
    if (rom.dat_name != current_dat) {
      current_dat = rom.dat_name;
      out << "── " << current_dat << " ──\n";
    }
    out << "  " << rom.game_name << " / " << rom.rom_name << "\n";
  }

  return out.str();
}

Result<std::string> ReportGenerator::missing_csv(database::Database& db,
                                                 std::optional<std::int64_t> sys) {
  auto missing = db.get_missing_roms(sys);
  if (!missing) {
    return std::unexpected(missing.error());
  }

  std::ostringstream out;
  out << "dat,game,rom,sha1\n";
  for (const auto& rom : *missing) {
    out << csv_escape(rom.dat_name) << "," << csv_escape(rom.game_name) << ","
        << csv_escape(rom.rom_name) << "," << rom.sha1 << "\n";
  }

  return out.str();
}

Result<std::string> ReportGenerator::missing_json(database::Database& db,
                                                  std::optional<std::int64_t> sys) {
  auto missing = db.get_missing_roms(sys);
  if (!missing) {
    return std::unexpected(missing.error());
  }

  nlohmann::json j = nlohmann::json::array();
  for (const auto& rom : *missing) {
    j.push_back({
        {"dat", rom.dat_name},
        {"game", rom.game_name},
        {"rom", rom.rom_name},
        {"sha1", rom.sha1},
    });
  }

  return j.dump(2);
}

// ═══════════════════════════════════════════════════════════════
// Duplicate File Reports
// ═══════════════════════════════════════════════════════════════

Result<std::string> ReportGenerator::duplicates_text(database::Database& db,
                                                     std::optional<std::int64_t> sys) {
  auto dupes = db.get_duplicate_files(sys);
  if (!dupes) {
    return std::unexpected(dupes.error());
  }

  std::ostringstream out;
  out << "\n═══ Duplicate Files (" << dupes->size() << ") ═══\n\n";

  std::string current_rom;
  for (const auto& d : *dupes) {
    if (d.rom_name != current_rom) {
      current_rom = d.rom_name;
      out << "── " << d.game_name << " / " << d.rom_name << " ──\n";
    }
    out << "  " << d.file_path << "\n";
  }

  if (dupes->empty()) {
    out << "  No duplicates found.\n";
  }

  return out.str();
}

Result<std::string> ReportGenerator::duplicates_csv(database::Database& db,
                                                    std::optional<std::int64_t> sys) {
  auto dupes = db.get_duplicate_files(sys);
  if (!dupes) {
    return std::unexpected(dupes.error());
  }

  std::ostringstream out;
  out << "file_path,rom_name,game_name\n";
  for (const auto& d : *dupes) {
    out << csv_escape(d.file_path) << "," << csv_escape(d.rom_name) << ","
        << csv_escape(d.game_name) << "\n";
  }

  return out.str();
}

Result<std::string> ReportGenerator::duplicates_json(database::Database& db,
                                                     std::optional<std::int64_t> sys) {
  auto dupes = db.get_duplicate_files(sys);
  if (!dupes) {
    return std::unexpected(dupes.error());
  }

  nlohmann::json j = nlohmann::json::array();
  for (const auto& d : *dupes) {
    j.push_back({
        {"file_path", d.file_path},
        {"rom_name", d.rom_name},
        {"game_name", d.game_name},
    });
  }

  return j.dump(2);
}

// ═══════════════════════════════════════════════════════════════
// Unverified File Reports
// ═══════════════════════════════════════════════════════════════

// Unverified files are files on disk that match no ROM in any DAT, so they have no
// DAT association. The dat_version_id parameter is intentionally unused here.
Result<std::string> ReportGenerator::unverified_text(
    database::Database& db, std::optional<std::int64_t> /*dat_version_id*/) {
  auto unverified = db.get_unverified_files();
  if (!unverified) {
    return std::unexpected(unverified.error());
  }

  std::ostringstream out;
  out << "\n═══ Unverified Files (" << unverified->size() << ") ═══\n\n";
  for (const auto& f : *unverified) {
    out << "  " << f.path << " (" << f.size << " bytes)\n";
  }

  if (unverified->empty()) {
    out << "  All files are verified.\n";
  }

  return out.str();
}

Result<std::string> ReportGenerator::unverified_csv(
    database::Database& db, std::optional<std::int64_t> /*dat_version_id*/) {
  auto unverified = db.get_unverified_files();
  if (!unverified) {
    return std::unexpected(unverified.error());
  }

  std::ostringstream out;
  out << "path,size,crc32,md5,sha1\n";

  for (const auto& f : *unverified) {
    out << csv_escape(f.path) << "," << f.size << "," << f.crc32 << "," << f.md5 << "," << f.sha1
        << "\n";
  }

  return out.str();
}

Result<std::string> ReportGenerator::unverified_json(
    database::Database& db, std::optional<std::int64_t> /*dat_version_id*/) {
  auto unverified = db.get_unverified_files();
  if (!unverified) {
    return std::unexpected(unverified.error());
  }

  nlohmann::json j = nlohmann::json::array();

  for (const auto& f : *unverified) {
    j.push_back({
        {"path", f.path},
        {"size", f.size},
        {"crc32", f.crc32},
        {"md5", f.md5},
        {"sha1", f.sha1},
    });
  }

  return j.dump(2);
}

} // namespace romulus::report
