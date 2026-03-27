#include <spdlog/spdlog.h>

#include <filesystem>
#include <iostream>

#include "romulus/classifier/classifier.hpp"
#include "romulus/dat_fetcher/dat_fetcher.hpp"
#include "romulus/dat_parser/dat_parser.hpp"
#include "romulus/database/database.hpp"
#include "romulus/hash_service/hash_service.hpp"
#include "romulus/matcher/matcher.hpp"
#include "romulus/report_generator/report_generator.hpp"
#include "romulus/rom_scanner/rom_scanner.hpp"

int main() {
  spdlog::info("ROMULUS v0.1.0 starting up...");

  romulus::Database db("romulus.db");
  if (auto res = db.initialize(); !res) {
    spdlog::error("Failed to initialize database");
    return 1;
  }

  spdlog::info("Database ready. Use the ROMULUS API to fetch DATs, scan ROMs, and generate reports.");
  spdlog::info("ROMULUS ready. Exiting demo.");
  return 0;
}
