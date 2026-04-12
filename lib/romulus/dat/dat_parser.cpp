#include "romulus/dat/dat_parser.hpp"

#include "romulus/core/logging.hpp"
#include "romulus/scanner/archive_service.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace romulus::dat {

namespace {

auto to_lower(std::string value) -> std::string {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

auto is_dat_entry(const std::string& entry_name) -> bool {
  const auto extension = to_lower(std::filesystem::path{entry_name}.extension().string());
  return extension == ".dat" || extension == ".xml";
}

auto collect_archive_dat_entries(const std::filesystem::path& dat_path)
    -> Result<std::vector<core::ArchiveEntry>> {
  auto entries = scanner::ArchiveService::list_entries(dat_path);
  if (!entries) {
    return std::unexpected(entries.error());
  }

  std::vector<core::ArchiveEntry> dat_entries;
  for (const auto& entry : *entries) {
    if (is_dat_entry(entry.name)) {
      dat_entries.push_back(entry);
    }
  }

  return dat_entries;
}

auto load_document_from_archive(const std::filesystem::path& dat_path,
                                pugi::xml_document& doc) -> Result<std::string> {
  auto dat_entries = collect_archive_dat_entries(dat_path);
  if (!dat_entries) {
    return std::unexpected(dat_entries.error());
  }

  if (dat_entries->empty()) {
    return std::unexpected(
        core::Error{core::ErrorCode::DatInvalidFormat,
                    "Archive does not contain a DAT/XML entry: " + dat_path.string()});
  }

  if (dat_entries->size() > 1) {
    std::string message = "Archive contains multiple DAT/XML entries: ";
    for (std::size_t index = 0; index < dat_entries->size(); ++index) {
      if (index > 0) {
        message += ", ";
      }
      message += dat_entries->at(index).name;
    }
    message += " in " + dat_path.string();
    return std::unexpected(core::Error{core::ErrorCode::DatInvalidFormat, std::move(message)});
  }

  std::string xml_content;
  auto stream = scanner::ArchiveService::stream_entry(
      dat_path,
      dat_entries->front().index,
      [&xml_content](const std::byte* data, std::size_t size) {
        xml_content.append(reinterpret_cast<const char*>(data), size);
      });
  if (!stream) {
    return std::unexpected(stream.error());
  }

  // PugiXML does not resolve external entities/DTDs, so XXE is not applicable here.
  // Keep default escape parsing enabled to preserve standard XML entity/character
  // reference decoding in DAT text and attributes.
  const auto result = doc.load_buffer(xml_content.data(), xml_content.size(), pugi::parse_default);
  if (!result) {
    return std::unexpected(core::Error{core::ErrorCode::DatParseError,
                                       "Failed to parse XML '" + dat_path.string() +
                                           std::string(core::k_ArchiveEntrySeparator) +
                                           dat_entries->front().name +
                                           "': " + result.description()});
  }

  return dat_entries->front().name;
}

auto load_document(const std::filesystem::path& dat_path,
                   pugi::xml_document& doc) -> Result<std::string> {
  if (scanner::ArchiveService::is_archive(dat_path)) {
    return load_document_from_archive(dat_path, doc);
  }

  // Keep default XML parsing behavior so standard escape sequences in DAT files
  // are decoded correctly. Any XXE-related static analysis concern should be
  // handled via documentation or suppression rather than changing parse flags.
  const auto result = doc.load_file(dat_path.c_str(), pugi::parse_default);
  if (!result) {
    return std::unexpected(
        core::Error{core::ErrorCode::DatParseError,
                    "Failed to parse XML '" + dat_path.string() + "': " + result.description()});
  }

  return dat_path.filename().string();
}

} // namespace

auto DatParser::parse(const std::filesystem::path& dat_path) -> Result<core::DatFile> {
  pugi::xml_document doc;
  auto loaded = load_document(dat_path, doc);
  if (!loaded) {
    return std::unexpected(loaded.error());
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

  ROMULUS_INFO(
      "Parsing DAT '{}' : name='{}', version='{}'", *loaded, header->name, header->version);

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
  // Note: description field removed from GameInfo — not stored in DB

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
