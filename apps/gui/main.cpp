#include "gui_app.hpp"

#include "romulus/core/logging.hpp"
#include "romulus/service/romulus_service.hpp"

#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

constexpr std::string_view k_DefaultDb = "romulus.db";

auto parse_no_gui(int argc, char** argv) -> bool {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--no-gui") == 0) {
      return true;
    }
  }
  return false;
}

auto parse_db_path(int argc, char** argv) -> std::string {
  for (int i = 1; i < argc - 1; ++i) {
    if (std::strcmp(argv[i], "--db") == 0) {
      return argv[i + 1];
    }
  }
  return std::string(k_DefaultDb);
}

auto parse_log_level(int argc, char** argv) -> std::string {
  for (int i = 1; i < argc - 1; ++i) {
    if (std::strcmp(argv[i], "--log-level") == 0) {
      return argv[i + 1];
    }
  }
  return "info";
}

void print_usage() {
  std::cout << "ROMULUS GUI — ROM Collection Verifier\n"
            << "\n"
            << "Usage: romulus-gui [options]\n"
            << "\n"
            << "Options:\n"
            << "  --no-gui       Do not launch the GUI (exit immediately)\n"
            << "  --db <path>    Path to SQLite database (default: romulus.db)\n"
            << "  --log-level <level>  Log level: trace/debug/info/warn/error (default: info)\n"
            << "  --help         Show this help message\n";
}

auto parse_help(int argc, char** argv) -> bool {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
      return true;
    }
  }
  return false;
}

} // namespace

int main(int argc, char** argv) {
  if (parse_help(argc, argv)) {
    print_usage();
    return 0;
  }

  auto log_level = parse_log_level(argc, argv);
  romulus::core::init_logging(log_level);

  if (parse_no_gui(argc, argv)) {
    ROMULUS_INFO("--no-gui flag detected. GUI will not launch.");
    std::cout << "ROMULUS service running in headless mode. GUI disabled.\n";
    return 0;
  }

  auto db_path = parse_db_path(argc, argv);
  ROMULUS_INFO("Starting ROMULUS GUI with database: {}", db_path);

  try {
    std::filesystem::path db_file(db_path);
    romulus::service::RomulusService svc(db_file);
    romulus::gui::GuiApp app(svc);
    app.run();
  } catch (const std::exception& e) {
    ROMULUS_ERROR("Fatal error: {}", e.what());
    std::cerr << "Fatal error: " << e.what() << '\n';
    return 1;
  }

  return 0;
}
