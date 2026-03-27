#include "romulus/dat_parser/dat_parser.hpp"

#include <pugixml.hpp>
#include <spdlog/spdlog.h>

namespace romulus {

std::expected<ParsedDat, Error> DatParser::parse(std::string_view xml_content) const {
  pugi::xml_document doc;
  const pugi::xml_parse_result result =
      doc.load_buffer(xml_content.data(), xml_content.size());

  if (!result) {
    spdlog::error("DatParser::parse: XML parse error: {}", result.description());
    return std::unexpected(Error::ParseInvalidXml);
  }

  const pugi::xml_node root = doc.child("datafile");
  if (!root) {
    spdlog::error("DatParser::parse: missing <datafile> root element");
    return std::unexpected(Error::ParseMissingElement);
  }

  // Parse header
  const pugi::xml_node header_node = root.child("header");
  if (!header_node) {
    spdlog::error("DatParser::parse: missing <header> element");
    return std::unexpected(Error::ParseMissingElement);
  }

  ParsedDat dat;
  dat.header.name = header_node.child_value("name");
  dat.header.version = header_node.child_value("version");
  dat.header.source_url = header_node.child_value("url");

  // Parse game entries
  for (const pugi::xml_node game_node : root.children("game")) {
    ParsedGame pg;
    pg.game.name = game_node.attribute("name").as_string();
    pg.game.system = dat.header.name;

    for (const pugi::xml_node rom_node : game_node.children("rom")) {
      Rom rom;
      rom.name = rom_node.attribute("name").as_string();
      rom.crc32 = rom_node.attribute("crc").as_string();
      rom.md5 = rom_node.attribute("md5").as_string();
      rom.sha1 = rom_node.attribute("sha1").as_string();
      rom.region = rom_node.attribute("region").as_string();
      pg.roms.push_back(std::move(rom));
    }

    dat.entries.push_back(std::move(pg));
  }

  spdlog::info("DatParser::parse: parsed {} game entries from '{}'", dat.entries.size(),
               dat.header.name);
  return dat;
}

}  // namespace romulus
