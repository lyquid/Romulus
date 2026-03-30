#pragma once

/// @file report_generator.hpp
/// @brief Generates collection reports in text, CSV, and JSON formats.

#include "romulus/core/error.hpp"
#include "romulus/core/types.hpp"

#include <optional>
#include <string>

namespace romulus::database {
class Database;
}

namespace romulus::report {

using romulus::core::Result;

/// Generates formatted reports from the database state.
class ReportGenerator final {
public:
  /// Generates a report of the specified type and format.
  /// @param db Database to query.
  /// @param type Report type (summary, missing, duplicates, unverified).
  /// @param format Output format (text, csv, json).
  /// @param system_id Optional system filter.
  /// @return Formatted report string.
  [[nodiscard]] static auto generate(
    database::Database& db,
    core::ReportType type,
    core::ReportFormat format,
    std::optional<std::int64_t> system_id = {}) -> Result<std::string>;

private:
  [[nodiscard]] static auto summary_text(database::Database& db, std::optional<std::int64_t> sys)
    -> Result<std::string>;
  [[nodiscard]] static auto summary_csv(database::Database& db, std::optional<std::int64_t> sys)
    -> Result<std::string>;
  [[nodiscard]] static auto summary_json(database::Database& db, std::optional<std::int64_t> sys)
    -> Result<std::string>;
  [[nodiscard]] static auto missing_text(database::Database& db, std::optional<std::int64_t> sys)
    -> Result<std::string>;
  [[nodiscard]] static auto missing_csv(database::Database& db, std::optional<std::int64_t> sys)
    -> Result<std::string>;
  [[nodiscard]] static auto missing_json(database::Database& db, std::optional<std::int64_t> sys)
    -> Result<std::string>;
};

} // namespace romulus::report
