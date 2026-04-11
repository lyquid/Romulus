#pragma once

/// @file classifier.hpp
/// @brief Classifies ROM status based on match results.

#include "romulus/core/error.hpp"
#include "romulus/core/types.hpp"

#include <optional>

namespace romulus::database {
class Database;
}

namespace romulus::engine {

using romulus::core::Result;

/// Classifies every ROM in the database based on rom matches.
class Classifier final {
public:
  /// Classifies all ROMs and logs the status counts.
  /// Status is computed dynamically from rom_matches + files (no separate status table).
  /// @param db Database with ROMs and rom_matches populated.
  /// @param dat_version_id Optional DAT version filter.
  [[nodiscard]] static auto classify_all(
      database::Database& db, std::optional<std::int64_t> dat_version_id = {}) -> Result<void>;
};

} // namespace romulus::engine
