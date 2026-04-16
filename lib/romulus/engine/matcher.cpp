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

  // Prepare all lookup + insert statements once before the ROM loop —
  // avoids O(N) sqlite3_prepare_v2 calls (up to 5 per ROM otherwise).
  auto sha1_stmt = db.prepare(
      "SELECT sha256, sha1, md5, crc32, size FROM global_roms WHERE sha1 = ?1 LIMIT 1");
  if (!sha1_stmt) {
    return std::unexpected(sha1_stmt.error());
  }
  auto md5_stmt = db.prepare(
      "SELECT sha256, sha1, md5, crc32, size FROM global_roms WHERE md5 = ?1 LIMIT 1");
  if (!md5_stmt) {
    return std::unexpected(md5_stmt.error());
  }
  // CRC32 may match multiple rows; ORDER BY sha1 ensures a deterministic winner.
  auto crc32_stmt = db.prepare(
      "SELECT sha256, sha1, md5, crc32, size FROM global_roms "
      "WHERE crc32 = ?1 ORDER BY sha1 LIMIT 1");
  if (!crc32_stmt) {
    return std::unexpected(crc32_stmt.error());
  }
  auto sha256_stmt = db.prepare(
      "SELECT sha256, sha1, md5, crc32, size FROM global_roms WHERE sha256 = ?1 LIMIT 1");
  if (!sha256_stmt) {
    return std::unexpected(sha256_stmt.error());
  }
  auto ins_stmt = db.prepare(
      "INSERT OR REPLACE INTO rom_matches (rom_id, global_rom_sha1, match_type) "
      "VALUES (?1, ?2, ?3)");
  if (!ins_stmt) {
    return std::unexpected(ins_stmt.error());
  }

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
      sha1_stmt->bind_blob_hex(1, rom.sha1);
      if (sha1_stmt->step()) {
        const auto g_sha1 = sha1_stmt->column_blob_hex(1);
        const auto g_md5 = sha1_stmt->column_blob_hex(2);
        const auto g_crc32 = sha1_stmt->column_blob_hex(3);
        sha1_stmt->reset();

        bool md5_match = rom.md5.empty() || g_md5.empty() || rom.md5 == g_md5;
        bool crc_match = rom.crc32.empty() || g_crc32.empty() || rom.crc32 == g_crc32;

        match.global_rom_sha1 = g_sha1;
        match.match_type =
            (md5_match && crc_match) ? core::MatchType::Exact : core::MatchType::Sha1Only;
        db.insert_rom_match_cached(*ins_stmt, match);
        results.push_back(match);
        continue;
      }
      sha1_stmt->reset();
    }

    // Priority 2: MD5 match
    // Uses the idx_global_roms_md5 index — O(log M) lookup.
    if (!rom.md5.empty()) {
      md5_stmt->bind_blob_hex(1, rom.md5);
      if (md5_stmt->step()) {
        match.global_rom_sha1 = md5_stmt->column_blob_hex(1);
        md5_stmt->reset();

        match.match_type = core::MatchType::Md5Only;
        db.insert_rom_match_cached(*ins_stmt, match);
        results.push_back(match);
        continue;
      }
      md5_stmt->reset();
    }

    // Priority 3: CRC32 match
    // Uses the idx_global_roms_crc32 index — O(log M) lookup.
    // ORDER BY sha1 in the query ensures a stable winner when multiple rows share a CRC32.
    if (!rom.crc32.empty()) {
      crc32_stmt->bind_blob_hex(1, rom.crc32);
      if (crc32_stmt->step()) {
        match.global_rom_sha1 = crc32_stmt->column_blob_hex(1);
        crc32_stmt->reset();

        match.match_type = core::MatchType::Crc32Only;
        db.insert_rom_match_cached(*ins_stmt, match);
        results.push_back(match);
        continue;
      }
      crc32_stmt->reset();
    }

    // Priority 4: SHA256 match (bonus enrichment — not standard in the DAT ecosystem)
    // Uses the idx_global_roms_sha256 index — O(log M) lookup.
    if (!rom.sha256.empty()) {
      sha256_stmt->bind_blob_hex(1, rom.sha256);
      if (sha256_stmt->step()) {
        match.global_rom_sha1 = sha256_stmt->column_blob_hex(1);
        sha256_stmt->reset();

        match.match_type = core::MatchType::Sha256Only;
        db.insert_rom_match_cached(*ins_stmt, match);
        results.push_back(match);
        continue;
      }
      sha256_stmt->reset();
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
