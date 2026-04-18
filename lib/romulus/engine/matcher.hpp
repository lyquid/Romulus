#pragma once

/// @file matcher.hpp
/// @brief Matches scanned files against ROM entries in the database.

#include "romulus/core/error.hpp"
#include "romulus/core/types.hpp"

#include <vector>

namespace romulus::database {
class Database;
}

namespace romulus::engine {

using romulus::core::Result;

/// Matches scanned files against known ROMs using hash comparison.
///
/// ## Match Priority (tier order, highest to lowest)
/// 1. SHA1 — gold standard; classifies as Exact when SHA256+MD5+CRC32 also agree
///    (each cross-check is skipped when either side lacks the hash), Sha1Only otherwise.
/// 2. SHA256 — used when SHA1 is absent from the DAT entry (some enriched DATs carry SHA256
///    without SHA1). Classifies as Exact when all remaining available hashes also agree,
///    Sha256Only otherwise.
/// 3. MD5 — weaker fallback.
/// 4. CRC32 — weakest; when multiple GlobalRoms share the same CRC32 the winner is selected
///    by pick_best_crc32_candidate() (see README § Match Priority Policy):
///    bare file > archive entry > shortest path > latest mtime > smallest SHA1.
///
/// No-match ROMs are recorded with MatchType::NoMatch and are not inserted into rom_matches.
class Matcher final {
public:
  /// Matches all scanned files in the DB against all known ROMs.
  /// @param db Database containing files, ROMs, and global_roms.
  /// @return Vector of match results stored in the rom_matches table.
  [[nodiscard]] static Result<std::vector<core::MatchResult>> match_all(database::Database& db);
};

} // namespace romulus::engine
