#include "romulus/scanner/hash_service.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace {

TEST(HashService, ComputesCorrectHashesForKnownContent) {
  // Create a temp file with known content
  auto temp = std::filesystem::temp_directory_path() / "romulus_test_hash.bin";
  {
    std::ofstream f(temp, std::ios::binary);
    f << "Hello, ROMULUS!";
  }

  auto result = romulus::scanner::HashService::compute_hashes(temp);
  ASSERT_TRUE(result.has_value()) << result.error().message;

  // Hashes should be non-empty hex strings
  EXPECT_FALSE(result->crc32.empty());
  EXPECT_FALSE(result->md5.empty());
  EXPECT_FALSE(result->sha1.empty());

  // CRC32 should be 8 hex chars
  EXPECT_EQ(result->crc32.size(), 8);
  // MD5 should be 32 hex chars
  EXPECT_EQ(result->md5.size(), 32);
  // SHA1 should be 40 hex chars
  EXPECT_EQ(result->sha1.size(), 40);

  std::filesystem::remove(temp);
}

TEST(HashService, ProducesDeterministicHashes) {
  auto temp = std::filesystem::temp_directory_path() / "romulus_test_hash2.bin";
  {
    std::ofstream f(temp, std::ios::binary);
    f << "Deterministic content for hashing";
  }

  auto result1 = romulus::scanner::HashService::compute_hashes(temp);
  auto result2 = romulus::scanner::HashService::compute_hashes(temp);

  ASSERT_TRUE(result1.has_value());
  ASSERT_TRUE(result2.has_value());

  EXPECT_EQ(result1->crc32, result2->crc32);
  EXPECT_EQ(result1->md5, result2->md5);
  EXPECT_EQ(result1->sha1, result2->sha1);

  std::filesystem::remove(temp);
}

TEST(HashService, ReturnsErrorForNonexistentFile) {
  auto result = romulus::scanner::HashService::compute_hashes("nonexistent_file.bin");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, romulus::core::ErrorCode::FileReadError);
}

TEST(HashService, HandlesEmptyFile) {
  auto temp = std::filesystem::temp_directory_path() / "romulus_test_empty.bin";
  {
    std::ofstream f(temp, std::ios::binary);
    // Empty file
  }

  auto result = romulus::scanner::HashService::compute_hashes(temp);
  ASSERT_TRUE(result.has_value()) << result.error().message;

  // Empty file should still produce valid hashes
  EXPECT_EQ(result->crc32.size(), 8);
  EXPECT_EQ(result->md5.size(), 32);
  EXPECT_EQ(result->sha1.size(), 40);

  std::filesystem::remove(temp);
}

} // namespace
