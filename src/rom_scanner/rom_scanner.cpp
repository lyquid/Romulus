#include "romulus/rom_scanner/rom_scanner.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <format>

namespace romulus {

RomScanner::RomScanner(HashService& hash_service) : hash_service_(&hash_service) {}

std::expected<std::vector<ScannedFile>, Error> RomScanner::scan(
    std::filesystem::path dir) const {
  if (!std::filesystem::exists(dir)) {
    spdlog::error("RomScanner::scan: directory not found: '{}'", dir.string());
    return std::unexpected(Error::ScanDirectoryNotFound);
  }
  if (!std::filesystem::is_directory(dir)) {
    spdlog::error("RomScanner::scan: path is not a directory: '{}'", dir.string());
    return std::unexpected(Error::ScanDirectoryNotFound);
  }

  std::vector<ScannedFile> results;
  std::error_code ec;

  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(dir, ec)) {
    if (ec) {
      spdlog::warn("RomScanner::scan: iterator error: {}", ec.message());
      ec.clear();
      continue;
    }
    if (!entry.is_regular_file(ec)) continue;

    const auto& p = entry.path();
    const auto file_size = entry.file_size(ec);
    if (ec) {
      spdlog::warn("RomScanner::scan: cannot stat '{}': {}", p.string(), ec.message());
      ec.clear();
      continue;
    }

    auto hash_result = hash_service_->hash_file(p);
    if (!hash_result) {
      spdlog::warn("RomScanner::scan: hash failed for '{}', skipping", p.string());
      continue;
    }

    const auto now = std::chrono::system_clock::now();
    const auto ts = std::format("{:%Y-%m-%dT%H:%M:%SZ}", now);

    ScannedFile sf;
    sf.path = p;
    sf.size = static_cast<std::int64_t>(file_size);
    sf.crc32 = std::move(hash_result->crc32);
    sf.md5 = std::move(hash_result->md5);
    sf.sha1 = std::move(hash_result->sha1);
    sf.last_scanned = ts;
    results.push_back(std::move(sf));
  }

  if (ec) {
    spdlog::error("RomScanner::scan: unexpected error: {}", ec.message());
    return std::unexpected(Error::ScanUnexpectedError);
  }

  spdlog::info("RomScanner::scan: found {} files in '{}'", results.size(), dir.string());
  return results;
}

}  // namespace romulus
