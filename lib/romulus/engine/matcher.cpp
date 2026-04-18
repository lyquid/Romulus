#include "romulus/engine/matcher.hpp"

#include "romulus/core/logging.hpp"
#include "romulus/database/database.hpp"

#include <algorithm>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace romulus::engine {

namespace {

/// Selects the best candidate when multiple GlobalRoms share the same CRC32.
///
/// Tiebreaker policy (mirrors README § Match Priority Policy — Within-Tier Rules):
///   1. Prefer the candidate whose best associated file is a bare (non-archive) file.
///   2. Among tied candidates, prefer the one with the shortest virtual path.
///   3. If path lengths are equal, prefer the latest last_write_time.
///   4. Deterministic fallback: lexicographically smallest SHA1.
///
/// Candidates with files on disk are always preferred over phantom candidates (no files).
[[nodiscard]] const core::GlobalRom* pick_best_crc32_candidate(
    const std::vector<const core::GlobalRom*>& candidates,
    const std::unordered_map<std::string_view,
                             std::vector<const core::FileTiebreakInfo*>,
                             core::StringViewHash,
                             std::equal_to<>>& files_by_sha1) {
  if (candidates.empty()) {
    return nullptr;
  }
  if (candidates.size() == 1) {
    return candidates.front();
  }

  // Compute a score for each candidate based on its best associated file.
  struct Scored {
    const core::GlobalRom* rom = nullptr;
    bool has_files = false;     ///< Any file exists for this global_rom
    bool has_bare_file = false; ///< At least one non-archive file exists
    std::size_t path_len = std::numeric_limits<std::size_t>::max(); ///< Shortest best-file path
    std::int64_t mtime = 0;                                         ///< Latest mtime of best file
  };

  std::vector<Scored> scored;
  scored.reserve(candidates.size());

  for (const auto* candidate : candidates) {
    Scored s{.rom = candidate};
    const auto it = files_by_sha1.find(candidate->sha1);
    if (it != files_by_sha1.end()) {
      for (const auto* file : it->second) {
        s.has_files = true;
        const bool is_bare = !file->is_archive_entry();
        const std::size_t plen = file->path.size();
        const std::int64_t mt = file->last_write_time;

        if (is_bare) {
          // Bare file always beats archive entry; within bare files prefer shorter path / newer
          if (!s.has_bare_file || plen < s.path_len ||
              (plen == s.path_len && mt > s.mtime)) {
            s.has_bare_file = true;
            s.path_len = plen;
            s.mtime = mt;
          }
        } else if (!s.has_bare_file) {
          // Only consider archive entries when no bare file has been seen yet
          if (plen < s.path_len || (plen == s.path_len && mt > s.mtime)) {
            s.path_len = plen;
            s.mtime = mt;
          }
        }
      }
    }
    scored.push_back(s);
  }

  // Pick the best score using the documented tiebreaker order.
  const Scored* best = &scored.front();
  for (const auto& s : scored) {
    if (&s == best) {
      continue;
    }
    // Tier 1: prefer candidates with any file over phantoms (no files)
    if (s.has_files && !best->has_files) {
      best = &s;
      continue;
    }
    if (!s.has_files && best->has_files) {
      continue;
    }
    // Tier 2: prefer bare (non-archive) file
    if (s.has_bare_file && !best->has_bare_file) {
      best = &s;
      continue;
    }
    if (!s.has_bare_file && best->has_bare_file) {
      continue;
    }
    // Tier 3: prefer shorter virtual path
    if (s.path_len < best->path_len) {
      best = &s;
      continue;
    }
    if (s.path_len > best->path_len) {
      continue;
    }
    // Tier 4: prefer newer file (latest last_write_time)
    if (s.mtime > best->mtime) {
      best = &s;
      continue;
    }
    if (s.mtime < best->mtime) {
      continue;
    }
    // Tier 5: deterministic fallback — lexicographically smallest SHA1
    if (s.rom->sha1 < best->rom->sha1) {
      best = &s;
    }
  }

  return best->rom;
}

} // namespace

Result<std::vector<core::MatchResult>> Matcher::match_all(database::Database& db) {
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

  // Load lean file metadata (sha1, path, entry_name, last_write_time) for CRC32 tiebreaking.
  // Using get_file_tiebreak_info() avoids loading full BLOB hash columns from the files table.
  auto files = db.get_file_tiebreak_info();
  if (!files) {
    return std::unexpected(files.error());
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

  // Index scanned files by SHA1 for CRC32 tiebreaking.
  std::unordered_map<std::string_view,
                     std::vector<const core::FileTiebreakInfo*>,
                     core::StringViewHash,
                     std::equal_to<>>
      files_by_sha1;
  files_by_sha1.reserve(files->size());
  for (const auto& file : *files) {
    if (!file.sha1.empty()) {
      files_by_sha1[file.sha1].push_back(&file);
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
        // Cross-validate all available hashes. A match is Exact only when every hash declared
        // by the DAT ROM agrees with the GlobalRom (or is absent from one side).
        bool sha256_match =
            rom.sha256.empty() || g_rom.sha256.empty() || rom.sha256 == g_rom.sha256;
        bool md5_match = rom.md5.empty() || g_rom.md5.empty() || rom.md5 == g_rom.md5;
        bool crc_match = rom.crc32.empty() || g_rom.crc32.empty() || rom.crc32 == g_rom.crc32;

        match.global_rom_sha1 = g_rom.sha1;
        if (sha256_match && md5_match && crc_match) {
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

    // Priority 2: SHA256 match — used when SHA1 is absent from the DAT entry (some enriched
    // DATs carry SHA256 without SHA1). Cross-validates all available hashes: if every hash
    // declared by the DAT agrees with the GlobalRom the match is Exact, otherwise Sha256Only.
    if (!rom.sha256.empty()) {
      const auto sha256_it = global_rom_by_sha256.find(rom.sha256);
      if (sha256_it != global_rom_by_sha256.end()) {
        const auto& g_rom = *sha256_it->second;
        bool sha1_match = rom.sha1.empty() || g_rom.sha1.empty() || rom.sha1 == g_rom.sha1;
        bool md5_match = rom.md5.empty() || g_rom.md5.empty() || rom.md5 == g_rom.md5;
        bool crc_match = rom.crc32.empty() || g_rom.crc32.empty() || rom.crc32 == g_rom.crc32;

        match.global_rom_sha1 = g_rom.sha1;
        if (sha1_match && md5_match && crc_match) {
          match.match_type = core::MatchType::Exact;
        } else {
          match.match_type = core::MatchType::Sha256Only;
        }

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

    // Priority 4: CRC32 match (weakest — may have multiple candidates).
    // When several GlobalRoms share the same CRC32 the tiebreaker in
    // pick_best_crc32_candidate() applies (see README § Match Priority Policy).
    if (!rom.crc32.empty()) {
      const auto crc32_it = global_roms_by_crc32.find(rom.crc32);
      if (crc32_it != global_roms_by_crc32.end() && !crc32_it->second.empty()) {
        const auto* best = pick_best_crc32_candidate(crc32_it->second, files_by_sha1);
        if (best != nullptr) {
          match.global_rom_sha1 = best->sha1;
          match.match_type = core::MatchType::Crc32Only;

          auto ins = db.insert_rom_match(match);
          if (!ins) {
            ROMULUS_WARN("Failed to insert match: {}", ins.error().message);
          }
          results.push_back(match);
          continue;
        }
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
