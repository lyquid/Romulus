#pragma once
#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>

#include "romulus/common.hpp"

namespace romulus {

struct HashResult {
  std::string crc32;
  std::string md5;
  std::string sha1;
};

class HashService {
 public:
  HashService() = default;
  ~HashService() = default;

  HashService(const HashService&) = delete;
  HashService& operator=(const HashService&) = delete;
  HashService(HashService&&) = default;
  HashService& operator=(HashService&&) = default;

  /// Hash a file on disk, returning CRC32/MD5/SHA1 as lowercase hex strings.
  [[nodiscard]] std::expected<HashResult, Error> hash_file(
      std::filesystem::path file_path) const;

  /// Hash an in-memory buffer, returning CRC32/MD5/SHA1 as lowercase hex strings.
  [[nodiscard]] HashResult hash_buffer(std::span<const std::byte> data) const;
};

}  // namespace romulus
