#include "romulus/scanner/hash_service.hpp"

#include <gtest/gtest.h>

#include <algorithm>
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

  // CRC32 should be 4 raw bytes; hex should be 8 chars
  EXPECT_EQ(result->crc32.size(), 4u);
  EXPECT_EQ(result->to_hex_crc32().size(), 8u);

  // MD5 should be 16 raw bytes; hex should be 32 chars
  EXPECT_EQ(result->md5.size(), 16u);
  EXPECT_EQ(result->to_hex_md5().size(), 32u);

  // SHA-1 should be 20 raw bytes; hex should be 40 chars
  EXPECT_EQ(result->sha1.size(), 20u);
  EXPECT_EQ(result->to_hex_sha1().size(), 40u);

  // SHA-256 should be 32 raw bytes; hex should be 64 chars
  EXPECT_EQ(result->sha256.size(), 32u);
  EXPECT_EQ(result->to_hex_sha256().size(), 64u);

  // Hex strings must be valid lowercase hex
  auto is_hex = [](const std::string& s) {
    return std::all_of(s.begin(), s.end(), [](char c) {
      return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
  };
  EXPECT_TRUE(is_hex(result->to_hex_crc32()));
  EXPECT_TRUE(is_hex(result->to_hex_md5()));
  EXPECT_TRUE(is_hex(result->to_hex_sha1()));
  EXPECT_TRUE(is_hex(result->to_hex_sha256()));

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

  // Empty file should still produce valid digests of the correct size
  EXPECT_EQ(result->crc32.size(), 4u);
  EXPECT_EQ(result->md5.size(), 16u);
  EXPECT_EQ(result->sha1.size(), 20u);
  EXPECT_EQ(result->sha256.size(), 32u);

  // Hex representations must also be the correct length
  EXPECT_EQ(result->to_hex_crc32().size(), 8u);
  EXPECT_EQ(result->to_hex_md5().size(), 32u);
  EXPECT_EQ(result->to_hex_sha1().size(), 40u);
  EXPECT_EQ(result->to_hex_sha256().size(), 64u);

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

TEST(HashService, StreamReaderRejectsNullReader) {
  // An empty (null) StreamReader must yield InvalidArgument, not throw.
  auto result = romulus::scanner::HashService::compute_hashes_stream(nullptr);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, romulus::core::ErrorCode::InvalidArgument);
}

TEST(HashService, KnownContentProducesExpectedHexDigests) {
  // Verify exact hash values for "Hello, ROMULUS!" against externally-computed
  // reference values, confirming the binary-to-hex conversion is correct.
  auto temp = std::filesystem::temp_directory_path() / "romulus_test_known.bin";
  {
    std::ofstream f(temp, std::ios::binary);
    f << "Hello, ROMULUS!";
  }

  auto result = romulus::scanner::HashService::compute_hashes(temp);
  ASSERT_TRUE(result.has_value()) << result.error().message;

  // Reference values computed with standard tools (crc32, md5sum, sha1sum, sha256sum).
  EXPECT_EQ(result->to_hex_crc32(), "f74da943");
  EXPECT_EQ(result->to_hex_md5(), "6d0546343d33a6bccb4ca445b7913175");
  EXPECT_EQ(result->to_hex_sha1(), "b9f880b5f15f39a151226c96745718a74335181f");
  EXPECT_EQ(result->to_hex_sha256(),
            "e809be3faae6a303c19936295c16c55216455f98ac3920632af71df9019c6ffd");

  std::filesystem::remove(temp);
}

} // namespace
