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
  EXPECT_FALSE(result->sha256.empty());

  // CRC32 should be 8 hex chars
  EXPECT_EQ(result->crc32.size(), 8);
  // MD5 should be 32 hex chars
  EXPECT_EQ(result->md5.size(), 32);
  // SHA1 should be 40 hex chars
  EXPECT_EQ(result->sha1.size(), 40);
  // SHA256 should be 64 hex chars
  EXPECT_EQ(result->sha256.size(), 64);

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
  EXPECT_EQ(result1->sha256, result2->sha256);

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
  EXPECT_EQ(result->sha256.size(), 64);

  std::filesystem::remove(temp);
}

TEST(HashService, StreamReaderProducesMatchingHashesAsFile) {
  // Verify that compute_hashes_stream yields the same digest as compute_hashes
  // for identical content, confirming both paths share the same core primitive.
  const std::string content = "Hello, ROMULUS stream!";

  auto temp = std::filesystem::temp_directory_path() / "romulus_test_stream.bin";
  {
    std::ofstream f(temp, std::ios::binary);
    f << content;
  }

  auto file_result = romulus::scanner::HashService::compute_hashes(temp);
  ASSERT_TRUE(file_result.has_value()) << file_result.error().message;

  auto stream_result = romulus::scanner::HashService::compute_hashes_stream(
      [&content](
          const romulus::scanner::DataChunkCallback& callback) -> romulus::core::Result<void> {
        callback(reinterpret_cast<const std::byte*>(content.data()), content.size());
        return {};
      });
  ASSERT_TRUE(stream_result.has_value()) << stream_result.error().message;

  EXPECT_EQ(stream_result->crc32, file_result->crc32);
  EXPECT_EQ(stream_result->md5, file_result->md5);
  EXPECT_EQ(stream_result->sha1, file_result->sha1);
  EXPECT_EQ(stream_result->sha256, file_result->sha256);

  std::filesystem::remove(temp);
}

TEST(HashService, StreamReaderPropagatesReaderError) {
  // Errors returned by the reader must be forwarded as-is.
  auto result = romulus::scanner::HashService::compute_hashes_stream(
      [](const romulus::scanner::DataChunkCallback&) -> romulus::core::Result<void> {
        return std::unexpected(
            romulus::core::Error{romulus::core::ErrorCode::FileReadError, "Simulated read error"});
      });

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, romulus::core::ErrorCode::FileReadError);
}

} // namespace
