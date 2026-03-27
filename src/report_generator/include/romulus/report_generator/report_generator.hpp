#pragma once
#include <expected>
#include <string>

#include "romulus/common.hpp"
#include "romulus/database/database.hpp"

namespace romulus {

class ReportGenerator {
 public:
  explicit ReportGenerator(Database& db);
  ~ReportGenerator() = default;

  ReportGenerator(const ReportGenerator&) = delete;
  ReportGenerator& operator=(const ReportGenerator&) = delete;
  ReportGenerator(ReportGenerator&&) = default;
  ReportGenerator& operator=(ReportGenerator&&) = default;

  /// Generate a human-readable text report with per-system ROM statistics.
  [[nodiscard]] std::expected<std::string, Error> generate_text_report() const;

 private:
  Database* db_;
};

}  // namespace romulus
