#include "romulus/scanner/hash_service.hpp"

#include "romulus/core/logging.hpp"
#include "romulus/scanner/archive_service.hpp"

#include <openssl/evp.h>

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace romulus::scanner {

namespace {

constexpr std::size_t k_BufferSize = 65536; // 64KB

/// CRC32 lookup table (IEEE polynomial).
constexpr auto make_crc32_table() -> std::array<std::uint32_t, 256> {
  std::array<std::uint32_t, 256> table{};
  for (std::uint32_t i = 0; i < 256; ++i) {
    std::uint32_t crc = i;
    for (int j = 0; j < 8; ++j) {
      if ((crc & 1) != 0) {
        crc = (crc >> 1) ^ 0xEDB88320U;
      } else {
        crc >>= 1;
      }
    }
    table[i] = crc;
  }
  return table;
}

constexpr auto k_Crc32Table = make_crc32_table();

/// RAII wrapper for OpenSSL EVP_MD_CTX.
struct EvpCtxDeleter {
  void operator()(EVP_MD_CTX* ctx) const {
    if (ctx != nullptr) {
      EVP_MD_CTX_free(ctx);
    }
  }
};
using EvpCtxPtr = std::unique_ptr<EVP_MD_CTX, EvpCtxDeleter>;

/// Holds all four hash contexts for single-pass computation.
struct HashContext {
  EvpCtxPtr md5_ctx;
  EvpCtxPtr sha1_ctx;
  EvpCtxPtr sha256_ctx;
  std::uint32_t crc32 = 0xFFFFFFFFU;

  /// Creates and initialises all EVP digest contexts.
  /// Returns an error if OpenSSL allocation or initialisation fails.
  static auto create() -> core::Result<HashContext> {
    HashContext ctx;
    ctx.md5_ctx.reset(EVP_MD_CTX_new());
    ctx.sha1_ctx.reset(EVP_MD_CTX_new());
    ctx.sha256_ctx.reset(EVP_MD_CTX_new());
    if (!ctx.md5_ctx || !ctx.sha1_ctx || !ctx.sha256_ctx) {
      return std::unexpected(
          core::Error{core::ErrorCode::HashComputeError, "Failed to allocate OpenSSL EVP_MD_CTX"});
    }
    if (EVP_DigestInit_ex(ctx.md5_ctx.get(), EVP_md5(), nullptr) != 1 ||
        EVP_DigestInit_ex(ctx.sha1_ctx.get(), EVP_sha1(), nullptr) != 1 ||
        EVP_DigestInit_ex(ctx.sha256_ctx.get(), EVP_sha256(), nullptr) != 1) {
      return std::unexpected(core::Error{core::ErrorCode::HashComputeError,
                                         "Failed to initialise OpenSSL EVP digest"});
    }
    return ctx;
  }

  void update(const void* data, std::size_t size) {
    // CRC32
    auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
      crc32 = k_Crc32Table[(crc32 ^ bytes[i]) & 0xFF] ^ (crc32 >> 8);
    }
    // MD5 + SHA1 + SHA256
    EVP_DigestUpdate(md5_ctx.get(), data, size);
    EVP_DigestUpdate(sha1_ctx.get(), data, size);
    EVP_DigestUpdate(sha256_ctx.get(), data, size);
  }

  auto finalize() -> core::HashDigest {
    crc32 ^= 0xFFFFFFFFU;

    std::array<unsigned char, EVP_MAX_MD_SIZE> md5_hash{};
    std::array<unsigned char, EVP_MAX_MD_SIZE> sha1_hash{};
    std::array<unsigned char, EVP_MAX_MD_SIZE> sha256_hash{};
    unsigned int md5_len = 0;
    unsigned int sha1_len = 0;
    unsigned int sha256_len = 0;

    EVP_DigestFinal_ex(md5_ctx.get(), md5_hash.data(), &md5_len);
    EVP_DigestFinal_ex(sha1_ctx.get(), sha1_hash.data(), &sha1_len);
    EVP_DigestFinal_ex(sha256_ctx.get(), sha256_hash.data(), &sha256_len);

    auto to_hex = [](const unsigned char* data, unsigned int len) {
      std::ostringstream hex;
      for (unsigned int i = 0; i < len; ++i) {
        hex << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(data[i]);
      }
      return hex.str();
    };

    std::ostringstream crc_hex;
    crc_hex << std::hex << std::setfill('0') << std::setw(8) << crc32;

    return core::HashDigest{
        .crc32 = crc_hex.str(),
        .md5 = to_hex(md5_hash.data(), md5_len),
        .sha1 = to_hex(sha1_hash.data(), sha1_len),
        .sha256 = to_hex(sha256_hash.data(), sha256_len),
    };
  }
};

} // namespace

auto HashService::compute_hashes_stream(const StreamReader& reader) -> Result<core::HashDigest> {
  auto ctx_result = HashContext::create();
  if (!ctx_result) {
    return std::unexpected(ctx_result.error());
  }
  auto& ctx = *ctx_result;

  auto feed_result =
      reader([&ctx](const std::byte* data, std::size_t size) { ctx.update(data, size); });

  if (!feed_result) {
    return std::unexpected(feed_result.error());
  }

  return ctx.finalize();
}

auto HashService::compute_hashes(const std::filesystem::path& file_path)
    -> Result<core::HashDigest> {
  auto result = compute_hashes_stream([&file_path](
                                          const DataChunkCallback& callback) -> Result<void> {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
      return std::unexpected(core::Error{core::ErrorCode::FileReadError,
                                         "Cannot open file for hashing: " + file_path.string()});
    }
    std::array<char, k_BufferSize> buffer{};
    while (file.read(buffer.data(), k_BufferSize) || file.gcount() > 0) {
      callback(reinterpret_cast<const std::byte*>(buffer.data()),
               static_cast<std::size_t>(file.gcount()));
      if (file.gcount() < static_cast<std::streamsize>(k_BufferSize)) {
        break;
      }
    }
    return {};
  });

  if (!result) {
    return result;
  }
  ROMULUS_DEBUG("Hashed '{}': CRC32={}, MD5={}, SHA1={}, SHA256={}",
                file_path.string(),
                result->crc32,
                result->md5,
                result->sha1,
                result->sha256);
  return result;
}

auto HashService::compute_hashes_archive(const std::filesystem::path& archive_path,
                                         std::size_t entry_index) -> Result<core::HashDigest> {
  auto result = compute_hashes_stream(
      [&archive_path, entry_index](const DataChunkCallback& callback) -> Result<void> {
        return ArchiveService::stream_entry(archive_path, entry_index, callback);
      });

  if (!result) {
    return result;
  }
  ROMULUS_DEBUG("Hashed '{}::[{}]': CRC32={}, MD5={}, SHA1={}, SHA256={}",
                archive_path.string(),
                entry_index,
                result->crc32,
                result->md5,
                result->sha1,
                result->sha256);
  return result;
}

} // namespace romulus::scanner
