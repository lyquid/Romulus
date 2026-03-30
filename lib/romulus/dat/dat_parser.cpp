#include "romulus/dat/dat_parser.hpp"

#include "romulus/core/logging.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <string>

namespace romulus::dat {

auto DatParser::parse(const std::filesystem::path& dat_path) -> Result<core::DatFile> {
  pugi::xml_document doc;
  pugi::xml_parse_result result = doc.load_file(dat_path.c_str());

  if (!result) {
    return std::unexpected(
        core::Error{core::ErrorCode::DatParseError,
                    "Failed to parse XML '" + dat_path.string() + "': " + result.description()});
  }

  auto datafile = doc.child("datafile");
  if (!datafile) {
    return std::unexpected(core::Error{core::ErrorCode::DatInvalidFormat,
                                       "Missing <datafile> root element in: " + dat_path.string()});
  }

  // Parse header
  auto header_node = datafile.child("header");
  if (!header_node) {
    return std::unexpected(core::Error{core::ErrorCode::DatInvalidFormat,
                                       "Missing <header> element in: " + dat_path.string()});
  }

  auto header = parse_header(&header_node);
  if (!header) {
    return std::unexpected(header.error());
  }

  ROMULUS_INFO("Parsing DAT: name='{}', version='{}'", header->name, header->version);

  // Parse all games
  core::DatFile dat_file;
  dat_file.header = std::move(*header);

  for (auto game_node = datafile.child("game"); game_node;
       game_node = game_node.next_sibling("game")) {
    auto game = parse_game(&game_node);
    if (!game) {
      ROMULUS_WARN("Skipping malformed game entry: {}", game.error().message);
      continue;
    }
    dat_file.games.push_back(std::move(*game));
  }

  ROMULUS_INFO("Parsed {} games from DAT", dat_file.games.size());
  return dat_file;
}

auto DatParser::parse_header(const void* node_ptr) -> Result<core::DatHeader> {
  const auto& node = *static_cast<const pugi::xml_node*>(node_ptr);

  core::DatHeader header;
  header.name = node.child_value("name");
  header.description = node.child_value("description");
  header.version = node.child_value("version");
  header.author = node.child_value("author");
  header.homepage = node.child_value("homepage");
  header.url = node.child_value("url");

  if (header.name.empty()) {
    return std::unexpected(core::Error{core::ErrorCode::DatInvalidFormat,
                                       "DAT header missing required <name> element"});
  }

  return header;
}

auto DatParser::parse_game(const void* node_ptr) -> Result<core::GameInfo> {
  const auto& node = *static_cast<const pugi::xml_node*>(node_ptr);

  core::GameInfo game;
  game.name = node.attribute("name").as_string();
  game.description = node.child_value("description");

  if (game.name.empty()) {
    return std::unexpected(
        core::Error{core::ErrorCode::DatInvalidFormat, "Game entry missing 'name' attribute"});
  }

  // Parse all <rom> children
  for (auto rom_node = node.child("rom"); rom_node; rom_node = rom_node.next_sibling("rom")) {
    core::RomInfo rom;
    rom.name = rom_node.attribute("name").as_string();
    rom.size = rom_node.attribute("size").as_llong();
    rom.crc32 = normalize_hash(rom_node.attribute("crc").as_string());
    rom.md5 = normalize_hash(rom_node.attribute("md5").as_string());
    rom.sha1 = normalize_hash(rom_node.attribute("sha1").as_string());

    // Try to extract region from the game name (e.g. "(USA)" or "(Europe)")
    auto name_str = game.name;
    if (auto pos = name_str.find('('); pos != std::string::npos) {
      auto end = name_str.find(')', pos);
      if (end != std::string::npos) {
        rom.region = name_str.substr(pos + 1, end - pos - 1);
      }
    }

    if (rom.name.empty()) {
      ROMULUS_WARN("Skipping ROM with no name in game '{}'", game.name);
      continue;
    }

    game.roms.push_back(std::move(rom));
  }

  return game;
}

auto DatParser::normalize_hash(std::string_view hash) -> std::string {
  std::string result;
  result.reserve(hash.size());

  for (char ch : hash) {
    if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
      result += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
  }

  return result;
}

} // namespace romulus::dat
