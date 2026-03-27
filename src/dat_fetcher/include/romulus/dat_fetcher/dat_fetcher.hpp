#pragma once
#include <expected>
#include <filesystem>
#include <string>

#include "romulus/common.hpp"

namespace romulus {

class DatFetcher {
 public:
  DatFetcher() = default;
  ~DatFetcher() = default;

  DatFetcher(const DatFetcher&) = delete;
  DatFetcher& operator=(const DatFetcher&) = delete;
  DatFetcher(DatFetcher&&) = default;
  DatFetcher& operator=(DatFetcher&&) = default;

  /// Reads a DAT file from the given path and returns its XML content.
  [[nodiscard]] std::expected<std::string, Error> fetch(std::filesystem::path dat_path) const;
};

}  // namespace romulus
