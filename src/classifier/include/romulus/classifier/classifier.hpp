#pragma once
#include <expected>
#include <vector>

#include "romulus/common.hpp"
#include "romulus/database/database.hpp"

namespace romulus {

class Classifier {
 public:
  explicit Classifier(Database& db);
  ~Classifier() = default;

  Classifier(const Classifier&) = delete;
  Classifier& operator=(const Classifier&) = delete;
  Classifier(Classifier&&) = default;
  Classifier& operator=(Classifier&&) = default;

  /// Determine Have/Missing/Duplicate/BadDump status for every known ROM.
  [[nodiscard]] std::expected<std::vector<RomStatusRecord>, Error> classify() const;

 private:
  Database* db_;
};

}  // namespace romulus
