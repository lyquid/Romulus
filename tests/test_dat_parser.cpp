#include <catch2/catch_test_macros.hpp>

#include "romulus/dat_parser/dat_parser.hpp"

static constexpr std::string_view kValidDat = R"xml(<?xml version="1.0"?>
<datafile>
  <header>
    <name>Nintendo - Game Boy</name>
    <version>2024-01-01</version>
    <url>https://nointro.org</url>
  </header>
  <game name="Tetris (World) (Rev 1)">
    <rom name="Tetris (World) (Rev 1).gb" crc="46df91ad" md5="982a2a0d" sha1="e7a717aa" region="World"/>
  </game>
  <game name="Super Mario Land (World)">
    <rom name="Super Mario Land (World).gb" crc="0e4de675" md5="f9a6e832" sha1="af0c2e4d" region="World"/>
    <rom name="Super Mario Land (World) [BIOS].gb" crc="deadbeef" md5="" sha1="" region="World"/>
  </game>
</datafile>
)xml";

static constexpr std::string_view kInvalidXml = "<<not valid xml>>";

static constexpr std::string_view kMissingHeader = R"xml(<?xml version="1.0"?>
<datafile>
  <game name="Test"/>
</datafile>
)xml";

TEST_CASE("DatParser parses valid No-Intro DAT", "[dat_parser]") {
  romulus::DatParser parser;
  auto result = parser.parse(kValidDat);
  REQUIRE(result.has_value());

  const auto& dat = *result;
  REQUIRE(dat.header.name == "Nintendo - Game Boy");
  REQUIRE(dat.header.version == "2024-01-01");
  REQUIRE(dat.header.source_url == "https://nointro.org");
  REQUIRE(dat.entries.size() == 2);

  REQUIRE(dat.entries[0].game.name == "Tetris (World) (Rev 1)");
  REQUIRE(dat.entries[0].roms.size() == 1);
  REQUIRE(dat.entries[0].roms[0].crc32 == "46df91ad");
  REQUIRE(dat.entries[0].roms[0].sha1 == "e7a717aa");

  REQUIRE(dat.entries[1].game.name == "Super Mario Land (World)");
  REQUIRE(dat.entries[1].roms.size() == 2);
}

TEST_CASE("DatParser returns error on invalid XML", "[dat_parser]") {
  romulus::DatParser parser;
  auto result = parser.parse(kInvalidXml);
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error() == romulus::Error::ParseInvalidXml);
}

TEST_CASE("DatParser returns error on missing header", "[dat_parser]") {
  romulus::DatParser parser;
  auto result = parser.parse(kMissingHeader);
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error() == romulus::Error::ParseMissingElement);
}

TEST_CASE("DatParser handles empty game list", "[dat_parser]") {
  constexpr std::string_view empty_dat = R"xml(<?xml version="1.0"?>
<datafile>
  <header>
    <name>Empty System</name>
    <version>1.0</version>
    <url></url>
  </header>
</datafile>
)xml";
  romulus::DatParser parser;
  auto result = parser.parse(empty_dat);
  REQUIRE(result.has_value());
  REQUIRE(result->entries.empty());
}
