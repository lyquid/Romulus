#include "synthetic_fixture_lab.hpp"

#include "romulus/core/error.hpp"
#include "romulus/core/types.hpp"
#include "romulus/scanner/hash_service.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace romulus::test {
namespace {

namespace fs = std::filesystem;
using core::Error;
using core::ErrorCode;
using core::HashDigest;
using core::Result;
using nlohmann::json; // NOLINT(misc-include-cleaner): nlohmann's macro API hides the provider.

/// A symbolic payload is the content identity shared by loose files, archive entries, and DAT
/// expectations. Keeping the human-readable bytes beside the generated hashes makes every
/// contradiction reviewable without a hash calculator.
struct Payload {
  std::string id;
  std::string bytes;
  HashDigest hashes;
};

enum class HashMode : std::uint8_t {
  Full,
  Md5Only,
  Crc32Only,
  ContradictoryMetadata,
};

/// One DAT expectation. `scenario_id` is deliberately stored as the DAT game name so audit tests
/// have a stable semantic key that is independent of database row order and generated IDs.
struct DatEntry {
  std::string scenario_id;
  std::string canonical_name;
  std::string payload_id;
  HashMode hash_mode = HashMode::Full;
  // Most entries use their own hashes. Only the explicit contradiction scenario names a second
  // payload, so make the normal aggregate state explicit for warning-as-error toolchains.
  std::optional<std::string> contradictory_md5_payload = std::nullopt;
};

struct DatDefinition {
  std::string filename;
  std::string name;
  std::string version;
  std::string description;
  std::vector<DatEntry> entries;
};

struct ArchiveMember {
  std::string entry_name;
  std::string payload_id;
};

struct ArchiveDeleter {
  void operator()(archive* writer) const {
    if (writer != nullptr) {
      archive_write_free(writer);
    }
  }
};

using ArchivePtr = std::unique_ptr<archive, ArchiveDeleter>;

Result<void> write_text_file(const fs::path& path, std::string_view contents) {
  fs::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream.is_open()) {
    return std::unexpected(
        Error{ErrorCode::FileWriteError, "Cannot write synthetic fixture: " + path.string()});
  }
  stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  if (!stream) {
    return std::unexpected(Error{ErrorCode::FileWriteError,
                                 "Failed while writing synthetic fixture: " + path.string()});
  }
  return {};
}

Result<HashDigest> hash_payload(std::string_view bytes) {
  // Use the production single-pass hashing primitive. The DAT generator therefore cannot silently
  // diverge from the hashes that RomScanner later computes from the materialized bytes.
  return scanner::HashService::compute_hashes_stream(
      [bytes](const scanner::DataChunkCallback& callback) -> Result<void> {
        callback(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size());
        return {};
      });
}

std::string xml_escape(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char character : value) {
    switch (character) {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '"':
        escaped += "&quot;";
        break;
      case '\'':
        escaped += "&apos;";
        break;
      default:
        escaped += character;
        break;
    }
  }
  return escaped;
}

Result<void> write_dat(const fs::path& path,
                       const DatDefinition& dat,
                       const std::map<std::string, Payload, std::less<>>& payloads) {
  std::ostringstream xml;
  xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<datafile>\n"
      << "  <header>\n"
      << "    <name>" << xml_escape(dat.name) << "</name>\n"
      << "    <description>" << xml_escape(dat.description) << "</description>\n"
      << "    <version>" << xml_escape(dat.version) << "</version>\n"
      << "    <author>Romulus synthetic fixture generator</author>\n"
      << "  </header>\n";

  for (const auto& entry : dat.entries) {
    const auto& payload = payloads.at(entry.payload_id);
    xml << "  <game name=\"" << xml_escape(entry.scenario_id) << "\">\n"
        << "    <description>" << xml_escape(entry.scenario_id) << "</description>\n"
        << "    <rom name=\"" << xml_escape(entry.canonical_name) << "\" size=\""
        << payload.bytes.size() << "\"";

    // Fallback entries intentionally omit stronger hashes. The contradictory entry intentionally
    // borrows MD5 from another real payload while retaining its own SHA-1, CRC32, and SHA-256.
    // That makes the inconsistency readable and deterministic rather than inventing random hex.
    switch (entry.hash_mode) {
      case HashMode::Full:
        xml << " crc=\"" << payload.hashes.to_hex_crc32() << "\""
            << " md5=\"" << payload.hashes.to_hex_md5() << "\""
            << " sha1=\"" << payload.hashes.to_hex_sha1() << "\""
            << " sha256=\"" << payload.hashes.to_hex_sha256() << "\"";
        break;
      case HashMode::Md5Only:
        xml << " md5=\"" << payload.hashes.to_hex_md5() << "\"";
        break;
      case HashMode::Crc32Only:
        xml << " crc=\"" << payload.hashes.to_hex_crc32() << "\"";
        break;
      case HashMode::ContradictoryMetadata: {
        const auto md5_payload_id = entry.contradictory_md5_payload.value_or(std::string{});
        const auto md5_source = payloads.find(md5_payload_id);
        if (md5_source == payloads.end()) {
          return std::unexpected(
              Error{ErrorCode::InvalidArgument,
                    "Contradictory DAT entry must name an existing MD5 payload"});
        }
        xml << " crc=\"" << payload.hashes.to_hex_crc32() << "\""
            << " md5=\"" << md5_source->second.hashes.to_hex_md5() << "\""
            << " sha1=\"" << payload.hashes.to_hex_sha1() << "\""
            << " sha256=\"" << payload.hashes.to_hex_sha256() << "\"";
        break;
      }
    }
    xml << "/>\n  </game>\n";
  }
  xml << "</datafile>\n";
  return write_text_file(path, xml.str());
}

Result<void> archive_failure(archive* writer, const fs::path& path, std::string_view operation) {
  const char* detail = archive_error_string(writer);
  return std::unexpected(
      Error{ErrorCode::FileWriteError,
            "Cannot " + std::string(operation) + " synthetic archive '" + path.string() +
                "': " + (detail != nullptr ? detail : "unknown libarchive error")});
}

Result<void> write_zip(const fs::path& path,
                       std::span<const ArchiveMember> members,
                       const std::map<std::string, Payload, std::less<>>& payloads) {
  fs::create_directories(path.parent_path());
  const ArchivePtr writer{archive_write_new()};
  if (!writer) {
    return std::unexpected(
        Error{ErrorCode::FileWriteError, "Cannot allocate writer for " + path.string()});
  }
  if (archive_write_set_format_zip(writer.get()) != ARCHIVE_OK) {
    return archive_failure(writer.get(), path, "configure");
  }
  if (archive_write_open_filename(writer.get(), path.string().c_str()) != ARCHIVE_OK) {
    return archive_failure(writer.get(), path, "open");
  }

  for (const auto& member : members) {
    const auto& bytes = payloads.at(member.payload_id).bytes;
    archive_entry* raw_entry = archive_entry_new();
    if (raw_entry == nullptr) {
      return std::unexpected(
          Error{ErrorCode::FileWriteError, "Cannot allocate archive entry for " + path.string()});
    }
    const std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(raw_entry,
                                                                              archive_entry_free);
    archive_entry_set_pathname(entry.get(), member.entry_name.c_str());
    archive_entry_set_filetype(entry.get(), AE_IFREG);
    archive_entry_set_perm(entry.get(), 0644);
    archive_entry_set_size(entry.get(), static_cast<la_int64_t>(bytes.size()));

    // ZIP metadata often embeds the current time, making byte-for-byte regeneration impossible.
    // A fixed epoch timestamp keeps archives deterministic across repeated local and CI runs.
    archive_entry_set_mtime(entry.get(), 0, 0);
    if (archive_write_header(writer.get(), entry.get()) != ARCHIVE_OK) {
      return archive_failure(writer.get(), path, "write an entry header to");
    }
    const auto written = archive_write_data(writer.get(), bytes.data(), bytes.size());
    if (written < 0 || static_cast<std::size_t>(written) != bytes.size()) {
      return archive_failure(writer.get(), path, "write entry bytes to");
    }
  }

  if (archive_write_close(writer.get()) != ARCHIVE_OK) {
    return archive_failure(writer.get(), path, "close");
  }
  return {};
}

std::vector<DatDefinition> dat_definitions() {
  return {
      {.filename = "Synthetic Console v1.dat",
       .name = "Synthetic Console",
       .version = "1",
       .description = "Synthetic Console hostile audit fixture v1",
       .entries =
           {
               {.scenario_id = "alpha",
                .canonical_name = "Alpha (World).rom",
                .payload_id = "alpha_v1"},
               {.scenario_id = "beta",
                .canonical_name = "Beta (World).rom",
                .payload_id = "beta_v1"},
               {.scenario_id = "gamma_missing",
                .canonical_name = "Gamma (World).rom",
                .payload_id = "gamma_v1"},
               {.scenario_id = "archive_exact",
                .canonical_name = "Archive Exact (World).rom",
                .payload_id = "archive_exact"},
               {.scenario_id = "archive_wrong_name",
                .canonical_name = "Archive Wrong (World).rom",
                .payload_id = "archive_wrong"},
               {.scenario_id = "md5_fallback",
                .canonical_name = "MD5 Fallback (World).rom",
                .payload_id = "md5_fallback",
                .hash_mode = HashMode::Md5Only},
               {.scenario_id = "crc32_fallback",
                .canonical_name = "CRC32 Fallback (World).rom",
                .payload_id = "crc32_fallback",
                .hash_mode = HashMode::Crc32Only},
               {.scenario_id = "hash_metadata_conflict",
                .canonical_name = "Conflict Probe (World).rom",
                .payload_id = "conflict_probe",
                .hash_mode = HashMode::ContradictoryMetadata,
                .contradictory_md5_payload = "alpha_v1"},
           }},
      {.filename = "Synthetic Console v2.dat",
       .name = "Synthetic Console",
       .version = "2",
       .description = "Synthetic Console hostile audit fixture v2",
       .entries =
           {
               // Same alpha bytes, different policy name: this is a rename, not bad content.
               {.scenario_id = "alpha",
                .canonical_name = "Alpha (USA).rom",
                .payload_id = "alpha_v1"},
               // Same logical/canonical name as v1, but expected bytes changed in v2.
               {.scenario_id = "beta",
                .canonical_name = "Beta (World).rom",
                .payload_id = "beta_v2"},
               // Delta exists on disk all along but is not expected until v2 is imported.
               {.scenario_id = "delta_added",
                .canonical_name = "Delta (World).rom",
                .payload_id = "delta_v1"},
               {.scenario_id = "archive_exact",
                .canonical_name = "Archive Exact (World).rom",
                .payload_id = "archive_exact"},
               {.scenario_id = "archive_wrong_name",
                .canonical_name = "Archive Wrong (World).rom",
                .payload_id = "archive_wrong"},
           }},
      {.filename = "Other Console.dat",
       .name = "Other Console",
       .version = "1",
       .description = "Synthetic unrelated console fixture",
       .entries =
           {
               {.scenario_id = "other_console_exclusive",
                .canonical_name = "Other Console Exclusive.rom",
                .payload_id = "other_console"},
           }},
  };
}

json expected_manifest(const std::map<std::string, Payload, std::less<>>& payloads) {
  json manifest = {
      {"schema_version", 1},
      {"lab_id", "romulus.synthetic.hostile.v1"},
      {"description",
       "Copyright-free fake payloads exercising Romulus scan/import/verify/audit semantics."},
  };

  manifest["payloads"] = json::object();
  for (const auto& [id, payload] : payloads) {
    manifest["payloads"][id] = {
        {"bytes", payload.bytes},
        {"size", payload.bytes.size()},
        {"crc32", payload.hashes.to_hex_crc32()},
        {"md5", payload.hashes.to_hex_md5()},
        {"sha1", payload.hashes.to_hex_sha1()},
        {"sha256", payload.hashes.to_hex_sha256()},
    };
  }

  manifest["dats"] = {
      {"synthetic_v1", "dats/Synthetic Console v1.dat"},
      {"synthetic_v2", "dats/Synthetic Console v2.dat"},
      {"other_console", "dats/Other Console.dat"},
  };
  manifest["sources"] = {
      {"alpha_canonical", "sources/clean/Alpha (World).rom"},
      {"beta_v1_wrong_name", "sources/messy_a/Beta Goblin.rom"},
      {"md5_fallback_wrong_name", "sources/messy_a/MD5 Goblin.rom"},
      {"crc32_fallback_wrong_name", "sources/messy_a/CRC Goblin.rom"},
      {"hash_metadata_conflict_wrong_name", "sources/messy_a/Conflict Goblin.rom"},
      {"beta_v2_canonical", "sources/messy_b/Beta (World).rom"},
      {"delta_added_in_v2", "sources/messy_b/Delta (World).rom"},
      {"other_console_exclusive", "sources/messy_b/Other Console Exclusive.rom"},
      {"globally_unknown", "sources/unknown/Mystery Goblin.rom"},
      {"alpha_duplicate_a", "sources/duplicates/Alpha Duplicate Copy A.rom"},
      {"alpha_duplicate_b", "sources/duplicates/Alpha Duplicate Copy B.rom"},
      {"alpha_archive_duplicate", "sources/archives/alpha-duplicate.zip::Alpha Archive Copy.rom"},
      {"archive_exact", "sources/archives/naming-cases.zip::Archive Exact (World).rom"},
      {"archive_wrong_name", "sources/archives/naming-cases.zip::Archive Goblin.rom"},
  };

  // Counts intentionally overlap: duplicate rows describe physical copies while the expectation
  // counts describe the selected DAT checklist. This mirrors DatAuditSummary rather than forcing
  // mutually-exclusive presentation buckets that the production model does not have.
  manifest["expected_audits"] = {
      {"synthetic_v1_all_dats_imported",
       {
           {"summary",
            {{"expected_roms", 8},
             {"correct", 2},
             {"missing", 1},
             {"wrong_name", 2},
             {"extra", 4},
             {"extra_known_other_dat", 3},
             {"globally_unknown", 1},
             {"duplicate_files", 4},
             {"crc_match", 1},
             {"md5_match", 2}}},
           {"expectations",
            {{"alpha", "Correct"},
             {"beta", "WrongCanonicalName"},
             {"gamma_missing", "Missing"},
             {"archive_exact", "Correct"},
             {"archive_wrong_name", "WrongCanonicalName"},
             {"md5_fallback", "Md5Match"},
             {"crc32_fallback", "CrcMatch"},
             {"hash_metadata_conflict", "Md5Match"}}},
       }},
      {"synthetic_v2_all_dats_imported",
       {
           {"summary",
            {{"expected_roms", 5},
             {"correct", 3},
             {"missing", 0},
             {"wrong_name", 2},
             {"extra", 6},
             {"extra_known_other_dat", 5},
             {"globally_unknown", 1},
             {"duplicate_files", 4}}},
           {"expectations",
            {{"alpha", "WrongCanonicalName"},
             {"beta", "Correct"},
             {"delta_added", "Correct"},
             {"archive_exact", "Correct"},
             {"archive_wrong_name", "WrongCanonicalName"}}},
       }},
  };

  manifest["vocabulary"] = {
      {"implemented",
       {"exact_name",
        "wrong_name",
        "missing",
        "extra_known_elsewhere",
        "globally_unknown",
        "duplicate_loose",
        "duplicate_loose_archive",
        "archive_exact_name",
        "archive_wrong_name",
        "dat_addition",
        "dat_removal",
        "dat_rename",
        "dat_hash_change",
        "md5_fallback",
        "crc32_fallback",
        "contradictory_hash_metadata"}},
      // These IDs reserve a common language for later roadmap work without implementing any
      // operation planner, Set Builder, DAT-history UI, or hardlink behavior in this issue.
      {"reserved",
       {"rename_destination_identical",
        "rename_destination_conflict",
        "planned_source_missing",
        "rename_already_applied",
        "source_a_incomplete",
        "source_b_incomplete",
        "sources_combined_complete",
        "multiple_valid_sources",
        "destination_partially_built",
        "hardlink_candidate_group"}},
  };
  manifest["known_limits"] = {
      {"sha256_dat_import",
       "Generated full DAT rows carry SHA-256, but the current DAT parser/importer does not "
       "persist "
       "that attribute."},
      {"hash_conflict_status",
       "The production matcher records one winning content identity per DAT ROM, so the "
       "multi-rom HashConflict classifier state remains covered by the lower-level DatAuditor "
       "unit fixture."},
      {"crc32_collision",
       "No brute-force CRC32 collision is generated; deterministic collision policy remains a "
       "lower-level matcher/database fixture."},
  };
  return manifest;
}

} // namespace

core::Result<SyntheticFixtureTree> generate_synthetic_fixture_lab(const fs::path& output_root) {
  try {
    if (output_root.empty()) {
      return std::unexpected(
          Error{ErrorCode::InvalidArgument, "Synthetic fixture output path must not be empty"});
    }

    const std::array<std::pair<std::string_view, std::string_view>, 12> payload_specs = {{
        {"alpha_v1", "ROMULUS_ALPHA_V1"},
        {"beta_v1", "ROMULUS_BETA_V1"},
        {"beta_v2", "ROMULUS_BETA_V2"},
        {"gamma_v1", "ROMULUS_GAMMA_V1"},
        {"delta_v1", "ROMULUS_DELTA_V1"},
        {"archive_exact", "ROMULUS_ARCHIVE_EXACT"},
        {"archive_wrong", "ROMULUS_ARCHIVE_WRONG"},
        {"md5_fallback", "ROMULUS_MD5_FALLBACK"},
        {"crc32_fallback", "ROMULUS_CRC32_FALLBACK"},
        {"conflict_probe", "ROMULUS_CONFLICT_PROBE"},
        {"other_console", "ROMULUS_OTHER_CONSOLE"},
        {"globally_unknown", "ROMULUS_UNKNOWN"},
    }};

    std::map<std::string, Payload, std::less<>> payloads;
    for (const auto& [id, bytes] : payload_specs) {
      auto hashes = hash_payload(bytes);
      if (!hashes) {
        return std::unexpected(hashes.error());
      }
      payloads.emplace(
          std::string{id},
          Payload{.id = std::string{id}, .bytes = std::string{bytes}, .hashes = *hashes});
    }

    SyntheticFixtureTree tree{
        .root = output_root,
        .dats = output_root / "dats",
        .sources = output_root / "sources",
        .manifest = output_root / "expected" / "scenario_manifest.json",
    };
    for (const auto& directory : {tree.dats,
                                  tree.sources / "clean",
                                  tree.sources / "messy_a",
                                  tree.sources / "messy_b",
                                  tree.sources / "duplicates",
                                  tree.sources / "unknown",
                                  tree.sources / "archives",
                                  tree.manifest.parent_path()}) {
      fs::create_directories(directory);
    }

    // Loose-file layout: alpha deliberately appears at three loose paths, and the canonical clean
    // path is shorter than both duplicate paths. That makes matched-file resolution deterministic
    // before the archive preference rule is even needed.
    const std::array<std::pair<fs::path, std::string_view>, 11> loose_files = {{
        {tree.sources / "clean" / "Alpha (World).rom", "alpha_v1"},
        {tree.sources / "messy_a" / "Beta Goblin.rom", "beta_v1"},
        {tree.sources / "messy_a" / "MD5 Goblin.rom", "md5_fallback"},
        {tree.sources / "messy_a" / "CRC Goblin.rom", "crc32_fallback"},
        {tree.sources / "messy_a" / "Conflict Goblin.rom", "conflict_probe"},
        {tree.sources / "messy_b" / "Beta (World).rom", "beta_v2"},
        {tree.sources / "messy_b" / "Delta (World).rom", "delta_v1"},
        {tree.sources / "messy_b" / "Other Console Exclusive.rom", "other_console"},
        {tree.sources / "duplicates" / "Alpha Duplicate Copy A.rom", "alpha_v1"},
        {tree.sources / "duplicates" / "Alpha Duplicate Copy B.rom", "alpha_v1"},
        {tree.sources / "unknown" / "Mystery Goblin.rom", "globally_unknown"},
    }};
    for (const auto& [path, payload_id] : loose_files) {
      auto written = write_text_file(path, payloads.at(std::string{payload_id}).bytes);
      if (!written) {
        return std::unexpected(written.error());
      }
    }

    const std::array alpha_archive = {
        ArchiveMember{.entry_name = "Alpha Archive Copy.rom", .payload_id = "alpha_v1"},
    };
    auto alpha_zip =
        write_zip(tree.sources / "archives" / "alpha-duplicate.zip", alpha_archive, payloads);
    if (!alpha_zip) {
      return std::unexpected(alpha_zip.error());
    }

    // One archive intentionally contains both naming outcomes so #98/#119 can extend the same
    // artifact later without creating another disconnected ZIP fixture.
    const std::array naming_archive = {
        ArchiveMember{.entry_name = "Archive Exact (World).rom", .payload_id = "archive_exact"},
        ArchiveMember{.entry_name = "Archive Goblin.rom", .payload_id = "archive_wrong"},
    };
    auto naming_zip =
        write_zip(tree.sources / "archives" / "naming-cases.zip", naming_archive, payloads);
    if (!naming_zip) {
      return std::unexpected(naming_zip.error());
    }

    for (const auto& dat : dat_definitions()) {
      auto written = write_dat(tree.dats / dat.filename, dat, payloads);
      if (!written) {
        return std::unexpected(written.error());
      }
    }

    const auto manifest_text = expected_manifest(payloads).dump(2) + '\n';
    auto manifest_written = write_text_file(tree.manifest, manifest_text);
    if (!manifest_written) {
      return std::unexpected(manifest_written.error());
    }
    return tree;
  } catch (const fs::filesystem_error& error) {
    return std::unexpected(
        Error{ErrorCode::FileWriteError,
              std::string{"Synthetic fixture filesystem error: "} + error.what()});
  } catch (const std::exception& error) {
    return std::unexpected(Error{
        ErrorCode::Unknown, "Synthetic fixture generation failed: " + std::string(error.what())});
  }
}

} // namespace romulus::test
