#pragma once
#include <expected>
#include <string_view>
#include <utility>
#include <vector>

#include "romulus/common.hpp"

namespace romulus {

/// A parsed game entry: the game metadata plus its list of ROMs.
struct ParsedGame {
  Game game;
  std::vector<Rom> roms;
};

/// Result of parsing a No-Intro DAT file.
struct ParsedDat {
  DatVersion header;
  std::vector<ParsedGame> entries;
};

class DatParser {
 public:
  DatParser() = default;
  ~DatParser() = default;

  DatParser(const DatParser&) = delete;
  DatParser& operator=(const DatParser&) = delete;
  DatParser(DatParser&&) = default;
  DatParser& operator=(DatParser&&) = default;

  /// Parse the given No-Intro XML content.
  [[nodiscard]] std::expected<ParsedDat, Error> parse(std::string_view xml_content) const;
};

}  // namespace romulus
