#include "romulus/engine/dat_auditor.hpp"

#include "romulus/database/database.hpp"

#include <string>
#include <string_view>

namespace romulus::engine {
namespace {

std::string virtual_path_filename(std::string_view path) {
  const auto archive_pos = path.rfind(core::k_ArchiveEntrySeparator);
  if (archive_pos != std::string_view::npos) {
    path.remove_prefix(archive_pos + core::k_ArchiveEntrySeparator.size());
  }

  const auto separator_pos = path.find_last_of("/\\");
  if (separator_pos != std::string_view::npos) {
    path.remove_prefix(separator_pos + 1);
  }
  return std::string(path);
}

core::DatAuditStatus audit_status(core::RomStatusType status,
                                  std::string_view canonical_name,
                                  std::string_view actual_name) {
  switch (status) {
    case core::RomStatusType::Verified:
      return canonical_name == actual_name ? core::DatAuditStatus::Correct
                                           : core::DatAuditStatus::WrongCanonicalName;
    case core::RomStatusType::Missing:
      return core::DatAuditStatus::Missing;
    case core::RomStatusType::CrcMatch:
      return core::DatAuditStatus::CrcMatch;
    case core::RomStatusType::Md5Match:
      return core::DatAuditStatus::Md5Match;
    case core::RomStatusType::HashConflict:
      return core::DatAuditStatus::HashConflict;
    case core::RomStatusType::Mismatch:
      return core::DatAuditStatus::Mismatch;
  }
  return core::DatAuditStatus::Missing;
}

std::string expectation_reason(core::DatAuditStatus status,
                               std::string_view canonical_name,
                               std::string_view actual_name) {
  switch (status) {
    case core::DatAuditStatus::Correct:
      return "Exact content match; the resolved physical name matches the DAT canonical name.";
    case core::DatAuditStatus::Missing:
      return "No scanned content matches this DAT expectation.";
    case core::DatAuditStatus::WrongCanonicalName:
      return "Exact content match, but resolved name '" + std::string(actual_name) +
             "' differs from canonical name '" + std::string(canonical_name) + "'.";
    case core::DatAuditStatus::CrcMatch:
      return "Only CRC32 matched; CRC32 alone is weak evidence and does not verify content.";
    case core::DatAuditStatus::Md5Match:
      return "A SHA-256, SHA-1, or MD5 fallback matched, but the available evidence is "
             "insufficient for exact content verification.";
    case core::DatAuditStatus::HashConflict:
      return "Multiple distinct content identities weakly match this DAT expectation.";
    case core::DatAuditStatus::Mismatch:
      return "A recorded match exists, but no scanned physical file currently backs it.";
    case core::DatAuditStatus::ExtraKnownOtherDat:
    case core::DatAuditStatus::ExtraUnknown:
    case core::DatAuditStatus::Duplicate:
      break;
  }
  return "Audit state could not be explained.";
}

std::string expectation_action(core::DatAuditStatus status) {
  switch (status) {
    case core::DatAuditStatus::Correct:
      return "No action needed.";
    case core::DatAuditStatus::Missing:
      return "Supply content matching the DAT hashes.";
    case core::DatAuditStatus::WrongCanonicalName:
      return "Rename candidate; execution is deferred to the shared operation planner.";
    case core::DatAuditStatus::CrcMatch:
    case core::DatAuditStatus::Md5Match:
      return "Inspect the hash evidence or rescan the source.";
    case core::DatAuditStatus::HashConflict:
      return "Inspect the competing content matches before taking any action.";
    case core::DatAuditStatus::Mismatch:
      return "Rescan the source location to confirm whether the matched file was removed.";
    case core::DatAuditStatus::ExtraKnownOtherDat:
    case core::DatAuditStatus::ExtraUnknown:
    case core::DatAuditStatus::Duplicate:
      break;
  }
  return "Review this audit finding; no filesystem action is available yet.";
}

void count_expectation(core::DatAuditSummary& summary, core::DatAuditStatus status) {
  switch (status) {
    case core::DatAuditStatus::Correct:
      ++summary.correct;
      break;
    case core::DatAuditStatus::Missing:
      ++summary.missing;
      break;
    case core::DatAuditStatus::WrongCanonicalName:
      ++summary.wrong_name;
      break;
    case core::DatAuditStatus::CrcMatch:
      ++summary.crc_match;
      break;
    case core::DatAuditStatus::Md5Match:
      ++summary.md5_match;
      break;
    case core::DatAuditStatus::HashConflict:
      ++summary.hash_conflict;
      break;
    case core::DatAuditStatus::Mismatch:
      ++summary.mismatch;
      break;
    case core::DatAuditStatus::ExtraKnownOtherDat:
    case core::DatAuditStatus::ExtraUnknown:
    case core::DatAuditStatus::Duplicate:
      break;
  }
}

} // namespace

core::Result<core::DatAudit> DatAuditor::audit(database::Database& db,
                                               std::int64_t dat_version_id) {
  auto roms = db.get_all_roms_with_status(dat_version_id);
  if (!roms) {
    return std::unexpected(roms.error());
  }
  auto matched_paths = db.get_matched_file_paths(dat_version_id);
  if (!matched_paths) {
    return std::unexpected(matched_paths.error());
  }
  auto duplicates = db.get_duplicate_files(dat_version_id);
  if (!duplicates) {
    return std::unexpected(duplicates.error());
  }
  auto globally_unknown = db.get_unverified_files();
  if (!globally_unknown) {
    return std::unexpected(globally_unknown.error());
  }
  auto other_dat_files = db.get_files_matching_other_dats(dat_version_id);
  if (!other_dat_files) {
    return std::unexpected(other_dat_files.error());
  }

  // Spell out every aggregate field because GCC's -Wmissing-field-initializers is promoted to an
  // error in CI. The explicit empty values also make the audit's initial state unambiguous.
  core::DatAudit result{
      .dat_version_id = dat_version_id,
      .summary = {},
      .rows = {},
  };
  result.summary.expected_roms = static_cast<std::int64_t>(roms->size());
  result.rows.reserve(roms->size() + duplicates->size() + globally_unknown->size() +
                      other_dat_files->size());

  for (const auto& [rom, classifier_status] : *roms) {
    std::string file_path;
    if (const auto it = matched_paths->find(rom.id); it != matched_paths->end()) {
      file_path = it->second;
    }
    const std::string actual_name = virtual_path_filename(file_path);

    // Naming policy is applied only to Verified (exact-content) expectations. Weak matches,
    // conflicts, and stale matches retain their classifier state regardless of filename.
    const auto status = audit_status(classifier_status, rom.name, actual_name);
    count_expectation(result.summary, status);
    result.rows.push_back({
        .status = status,
        .rom_id = rom.id,
        .game_name = rom.game_name,
        .canonical_name = rom.name,
        .actual_name = actual_name,
        .file_path = std::move(file_path),
        .reason = expectation_reason(status, rom.name, actual_name),
        .suggested_action = expectation_action(status),
        .size = rom.size,
    });
  }

  for (const auto& duplicate : *duplicates) {
    ++result.summary.duplicate_files;
    result.rows.push_back({
        .status = core::DatAuditStatus::Duplicate,
        .rom_id = 0,
        .game_name = duplicate.game_name,
        .canonical_name = duplicate.rom_name,
        .actual_name = virtual_path_filename(duplicate.file_path),
        .file_path = duplicate.file_path,
        .reason = "This matched content exists in " + std::to_string(duplicate.copy_count) +
                  " physical locations; this row identifies one copy.",
        .suggested_action =
            "Review the copies; optimization is deferred and no deletion is proposed.",
        .size = 0,
    });
  }

  for (const auto& extra : *other_dat_files) {
    ++result.summary.extra;
    ++result.summary.extra_known_other_dat;
    result.rows.push_back({
        .status = core::DatAuditStatus::ExtraKnownOtherDat,
        .rom_id = 0,
        .game_name = {},
        .canonical_name = {},
        .actual_name = virtual_path_filename(extra.file.path),
        .file_path = extra.file.path,
        .reason = "No content match in the selected DAT; this content matches imported DAT(s): " +
                  extra.matching_dat_names + ".",
        .suggested_action =
            "Review the selected DAT scope; keep or move only through a future operation plan.",
        .size = extra.file.size,
    });
  }

  for (const auto& extra : *globally_unknown) {
    ++result.summary.extra;
    ++result.summary.globally_unknown;
    result.rows.push_back({
        .status = core::DatAuditStatus::ExtraUnknown,
        .rom_id = 0,
        .game_name = {},
        .canonical_name = {},
        .actual_name = virtual_path_filename(extra.path),
        .file_path = extra.path,
        .reason = "No imported DAT matches this file's content; it is globally unknown.",
        .suggested_action = "Identify or quarantine only through a future reviewed operation plan.",
        .size = extra.size,
    });
  }

  return result;
}

} // namespace romulus::engine
