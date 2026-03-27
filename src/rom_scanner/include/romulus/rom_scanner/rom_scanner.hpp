#pragma once
#include <expected>
#include <filesystem>
#include <vector>

#include "romulus/common.hpp"
#include "romulus/hash_service/hash_service.hpp"

namespace romulus {

class RomScanner {
 public:
  explicit RomScanner(HashService& hash_service);
  ~RomScanner() = default;

  RomScanner(const RomScanner&) = delete;
  RomScanner& operator=(const RomScanner&) = delete;
  RomScanner(RomScanner&&) = default;
  RomScanner& operator=(RomScanner&&) = default;

  /// Recursively scan a directory, hashing every regular file found.
  [[nodiscard]] std::expected<std::vector<ScannedFile>, Error> scan(
      std::filesystem::path dir) const;

 private:
  HashService* hash_service_;
};

}  // namespace romulus
