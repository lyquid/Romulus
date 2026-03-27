#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <span>

#include "romulus/hash_service/hash_service.hpp"

TEST_CASE("HashService hashes empty buffer", "[hash_service]") {
  romulus::HashService hs;
  const std::byte empty_data{};
  const auto result = hs.hash_buffer(std::span<const std::byte>(&empty_data, 0));

  // CRC32 of empty = 0x00000000
  REQUIRE(result.crc32 == "00000000");
  // MD5 of empty
  REQUIRE(result.md5 == "d41d8cd98f00b204e9800998ecf8427e");
  // SHA1 of empty
  REQUIRE(result.sha1 == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
}

TEST_CASE("HashService hashes known buffer", "[hash_service]") {
  romulus::HashService hs;
  // "abc" in bytes
  const std::array<std::byte, 3> data{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
  const auto result = hs.hash_buffer(std::span<const std::byte>(data));

  // CRC32("abc") = 352441C2
  REQUIRE(result.crc32 == "352441c2");
  // MD5("abc") = 900150983cd24fb0d6963f7d28e17f72
  REQUIRE(result.md5 == "900150983cd24fb0d6963f7d28e17f72");
  // SHA1("abc") = a9993e364706816aba3e25717850c26c9cd0d89d
  REQUIRE(result.sha1 == "a9993e364706816aba3e25717850c26c9cd0d89d");
}

TEST_CASE("HashService hashes file on disk", "[hash_service]") {
  const std::filesystem::path test_file = "test_hash_file.bin";
  {
    std::ofstream ofs(test_file, std::ios::binary);
    ofs << "abc";
  }

  romulus::HashService hs;
  auto result = hs.hash_file(test_file);
  REQUIRE(result.has_value());
  REQUIRE(result->crc32 == "352441c2");
  REQUIRE(result->md5 == "900150983cd24fb0d6963f7d28e17f72");
  REQUIRE(result->sha1 == "a9993e364706816aba3e25717850c26c9cd0d89d");

  std::filesystem::remove(test_file);
}

TEST_CASE("HashService returns error for missing file", "[hash_service]") {
  romulus::HashService hs;
  auto result = hs.hash_file("nonexistent_file_xyz.bin");
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error() == romulus::Error::HashFileNotFound);
}
