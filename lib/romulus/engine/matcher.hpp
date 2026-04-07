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
/// Priority: SHA1 > MD5 > CRC32 (most specific to least).
class Matcher final {
public:
  /// Matches all scanned files in the DB against all known ROMs.
  /// @param db Database containing files and ROMs.
  /// @return Vector of match results stored in rom_matches table.
  [[nodiscard]] static auto match_all(database::Database& db)
      -> Result<std::vector<core::MatchResult>>;
};

} // namespace romulus::engine
