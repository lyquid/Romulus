#include "romulus/dat/dat_fetcher.hpp"

#include "romulus/core/logging.hpp"
#include "romulus/database/database.hpp"

#include <openssl/evp.h>

#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace romulus::dat {

Result<std::filesystem::path> DatFetcher::validate_local(const std::filesystem::path& path) {
  if (!std::filesystem::exists(path)) {
    return std::unexpected(
        core::Error{core::ErrorCode::FileNotFound, "DAT file not found: " + path.string()});
  }

  if (!std::filesystem::is_regular_file(path)) {
    return std::unexpected(core::Error{core::ErrorCode::InvalidArgument,
                                       "Path is not a regular file: " + path.string()});
  }

  auto canonical = std::filesystem::canonical(path);
  ROMULUS_INFO("Validated DAT file: {}", canonical.string());
  return canonical;
}

Result<std::string> DatFetcher::compute_sha256(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return std::unexpected(core::Error{core::ErrorCode::FileReadError,
                                       "Cannot open file for SHA-256: " + path.string()});
  }

  auto* ctx = EVP_MD_CTX_new();
  if (ctx == nullptr) {
    return std::unexpected(
        core::Error{core::ErrorCode::HashComputeError, "Failed to create EVP_MD_CTX"});
  }

  EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);

  constexpr std::size_t k_BufferSize = 65536;
  std::array<char, k_BufferSize> buffer{};

  while (file.read(buffer.data(), k_BufferSize) || file.gcount() > 0) {
    EVP_DigestUpdate(ctx, buffer.data(), static_cast<std::size_t>(file.gcount()));

    if (file.gcount() < static_cast<std::streamsize>(k_BufferSize)) {
      break;
    }
  }

  std::array<unsigned char, EVP_MAX_MD_SIZE> hash{};
  unsigned int hash_len = 0;
  EVP_DigestFinal_ex(ctx, hash.data(), &hash_len);
  EVP_MD_CTX_free(ctx);

  std::ostringstream hex;
  for (unsigned int i = 0; i < hash_len; ++i) {
    hex << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(hash[i]);
  }

  return hex.str();
}

Result<bool> DatFetcher::has_version_changed(const std::filesystem::path& path,
                                             std::string_view dat_name,
                                             database::Database& db) {
  auto dat_sha256 = compute_sha256(path);
  if (!dat_sha256) {
    return std::unexpected(dat_sha256.error());
  }

  // Check by DAT SHA-256 — uniquely identifies a DAT file regardless of name/version.
  auto existing = db.find_dat_version_by_sha256(*dat_sha256);
  if (!existing) {
    return std::unexpected(existing.error());
  }

  if (!existing->has_value()) {
    ROMULUS_INFO("No previous DAT version found for '{}' — treating as new", dat_name);
    return true;
  }

  ROMULUS_INFO("DAT file unchanged for '{}'", dat_name);
  return false;
}

} // namespace romulus::dat
