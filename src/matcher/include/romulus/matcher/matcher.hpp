#pragma once
#include <expected>
#include <vector>

#include "romulus/common.hpp"
#include "romulus/database/database.hpp"

namespace romulus {

class Matcher {
 public:
  explicit Matcher(Database& db);
  ~Matcher() = default;

  Matcher(const Matcher&) = delete;
  Matcher& operator=(const Matcher&) = delete;
  Matcher(Matcher&&) = default;
  Matcher& operator=(Matcher&&) = default;

  /// Match scanned files against known ROMs in the database.
  /// Matching priority: Exact (sha1+name) > CrcOnly (crc32) > Renamed (sha1, diff name).
  [[nodiscard]] std::expected<std::vector<FileMatch>, Error> match(
      std::vector<ScannedFile> files) const;

 private:
  Database* db_;
};

}  // namespace romulus
