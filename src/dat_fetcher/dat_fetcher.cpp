#include "romulus/dat_fetcher/dat_fetcher.hpp"

#include <spdlog/spdlog.h>

#include <fstream>
#include <sstream>

namespace romulus {

std::expected<std::string, Error> DatFetcher::fetch(std::filesystem::path dat_path) const {
  if (!std::filesystem::exists(dat_path)) {
    spdlog::error("DatFetcher::fetch: file not found: '{}'", dat_path.string());
    return std::unexpected(Error::FetchFileNotFound);
  }

  std::ifstream ifs(dat_path, std::ios::in | std::ios::binary);
  if (!ifs.is_open()) {
    spdlog::error("DatFetcher::fetch: cannot open file: '{}'", dat_path.string());
    return std::unexpected(Error::FetchReadError);
  }

  std::ostringstream oss;
  oss << ifs.rdbuf();
  if (ifs.fail() && !ifs.eof()) {
    spdlog::error("DatFetcher::fetch: read error on: '{}'", dat_path.string());
    return std::unexpected(Error::FetchReadError);
  }

  spdlog::info("DatFetcher::fetch: loaded '{}' ({} bytes)", dat_path.string(),
               oss.str().size());
  return oss.str();
}

}  // namespace romulus
