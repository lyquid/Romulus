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

/// Classifies every ROM in the database based on file matches.
class Classifier final {
public:
  /// Classifies all ROMs and updates the rom_status table.
  /// @param db Database with ROMs and file_matches populated.
  /// @param system_id Optional system filter.
  [[nodiscard]] static auto classify_all(
      database::Database& db, std::optional<std::int64_t> system_id = {}) -> Result<void>;
};

} // namespace romulus::engine
