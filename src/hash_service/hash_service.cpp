#include "romulus/hash_service/hash_service.hpp"

#include <openssl/evp.h>
#include <spdlog/spdlog.h>

#include <array>
#include <cstdio>
#include <fstream>
#include <vector>

namespace romulus {

namespace {

// ----- CRC32 implementation with constexpr lookup table -----

static constexpr std::array<std::uint32_t, 256> make_crc_table() noexcept {
  std::array<std::uint32_t, 256> tbl{};
  for (std::uint32_t i = 0U; i < 256U; ++i) {
    std::uint32_t c = i;
    for (int j = 0; j < 8; ++j) {
      c = (c & 1U) != 0U ? (0xEDB88320U ^ (c >> 1U)) : (c >> 1U);
    }
    tbl[static_cast<std::size_t>(i)] = c;
  }
  return tbl;
}

static constexpr auto kCrcTable = make_crc_table();

std::uint32_t compute_crc32(std::span<const std::byte> data) noexcept {
  std::uint32_t crc = 0xFFFFFFFFU;
  for (const auto b : data) {
    const auto idx =
        static_cast<std::size_t>((crc ^ static_cast<std::uint32_t>(b)) & 0xFFU);
    crc = kCrcTable[idx] ^ (crc >> 8U);
  }
  return crc ^ 0xFFFFFFFFU;
}

// ----- Hex encoding -----

std::string to_hex(const unsigned char* data, std::size_t len) {
  std::string out;
  out.reserve(len * 2U);
  static constexpr char kHex[] = "0123456789abcdef";
  for (std::size_t i = 0; i < len; ++i) {
    out.push_back(kHex[(data[i] >> 4U) & 0x0FU]);
    out.push_back(kHex[data[i] & 0x0FU]);
  }
  return out;
}

std::string uint32_to_hex8(std::uint32_t val) {
  char buf[9];
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  std::snprintf(buf, sizeof(buf), "%08x", val);
  return std::string(buf);
}

// ----- OpenSSL EVP helper -----

struct EvpCtxDeleter {
  void operator()(EVP_MD_CTX* ctx) const noexcept { EVP_MD_CTX_free(ctx); }
};
using EvpCtxPtr = std::unique_ptr<EVP_MD_CTX, EvpCtxDeleter>;

struct DigestPair {
  std::string md5;
  std::string sha1;
};

DigestPair compute_md5_sha1(std::span<const std::byte> data) {
  EvpCtxPtr md5_ctx(EVP_MD_CTX_new());
  EvpCtxPtr sha1_ctx(EVP_MD_CTX_new());

  EVP_DigestInit_ex(md5_ctx.get(), EVP_md5(), nullptr);
  EVP_DigestInit_ex(sha1_ctx.get(), EVP_sha1(), nullptr);

  EVP_DigestUpdate(md5_ctx.get(), data.data(), data.size());
  EVP_DigestUpdate(sha1_ctx.get(), data.data(), data.size());

  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int digest_len = 0U;

  DigestPair result;

  EVP_DigestFinal_ex(md5_ctx.get(), digest.data(), &digest_len);
  result.md5 = to_hex(digest.data(), static_cast<std::size_t>(digest_len));

  EVP_DigestFinal_ex(sha1_ctx.get(), digest.data(), &digest_len);
  result.sha1 = to_hex(digest.data(), static_cast<std::size_t>(digest_len));

  return result;
}

}  // namespace

HashResult HashService::hash_buffer(std::span<const std::byte> data) const {
  const std::uint32_t crc = compute_crc32(data);
  auto [md5, sha1] = compute_md5_sha1(data);
  return HashResult{uint32_to_hex8(crc), std::move(md5), std::move(sha1)};
}

std::expected<HashResult, Error> HashService::hash_file(
    std::filesystem::path file_path) const {
  if (!std::filesystem::exists(file_path)) {
    spdlog::error("HashService::hash_file: not found: '{}'", file_path.string());
    return std::unexpected(Error::HashFileNotFound);
  }

  std::ifstream ifs(file_path, std::ios::in | std::ios::binary);
  if (!ifs.is_open()) {
    spdlog::error("HashService::hash_file: cannot open: '{}'", file_path.string());
    return std::unexpected(Error::HashReadError);
  }

  // Stream through file updating all three digests simultaneously
  EvpCtxPtr md5_ctx(EVP_MD_CTX_new());
  EvpCtxPtr sha1_ctx(EVP_MD_CTX_new());
  EVP_DigestInit_ex(md5_ctx.get(), EVP_md5(), nullptr);
  EVP_DigestInit_ex(sha1_ctx.get(), EVP_sha1(), nullptr);

  std::uint32_t crc = 0xFFFFFFFFU;

  constexpr std::size_t kBufSize = 65536U;
  std::vector<std::byte> buf(kBufSize);

  while (ifs) {
    ifs.read(reinterpret_cast<char*>(buf.data()),
             static_cast<std::streamsize>(kBufSize));
    const auto n = static_cast<std::size_t>(ifs.gcount());
    if (n == 0U) break;

    const std::span<const std::byte> chunk(buf.data(), n);
    for (const auto b : chunk) {
      const auto idx =
          static_cast<std::size_t>((crc ^ static_cast<std::uint32_t>(b)) & 0xFFU);
      crc = kCrcTable[idx] ^ (crc >> 8U);
    }
    EVP_DigestUpdate(md5_ctx.get(), chunk.data(), chunk.size());
    EVP_DigestUpdate(sha1_ctx.get(), chunk.data(), chunk.size());
  }

  if (ifs.bad()) {
    spdlog::error("HashService::hash_file: read error: '{}'", file_path.string());
    return std::unexpected(Error::HashReadError);
  }

  crc ^= 0xFFFFFFFFU;

  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int digest_len = 0U;

  EVP_DigestFinal_ex(md5_ctx.get(), digest.data(), &digest_len);
  std::string md5_hex = to_hex(digest.data(), static_cast<std::size_t>(digest_len));

  EVP_DigestFinal_ex(sha1_ctx.get(), digest.data(), &digest_len);
  std::string sha1_hex = to_hex(digest.data(), static_cast<std::size_t>(digest_len));

  return HashResult{uint32_to_hex8(crc), std::move(md5_hex), std::move(sha1_hex)};
}

}  // namespace romulus
