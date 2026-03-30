#include "romulus/core/logging.hpp"
#include "romulus/core/types.hpp"
#include "romulus/service/romulus_service.hpp"

#include <CLI/CLI.hpp>

#include <filesystem>
#include <iostream>
#include <string>

namespace {

constexpr std::string_view k_Version = "0.1.0";
constexpr std::string_view k_DefaultDb = "romulus.db";

auto get_db_path(const std::string& db_flag) -> std::filesystem::path {
  if (!db_flag.empty()) {
    return db_flag;
  }
  // Default: next to the executable
  return k_DefaultDb;
}

} // namespace

int main(int argc, char** argv) {
  CLI::App app{"ROMULUS — ROM collection verification system\n"
               "Imposes order on chaos.",
               "romulus"};
  app.set_version_flag("--version", std::string(k_Version));

  std::string db_path_str;
  std::string log_level = "info";
  app.add_option("--db", db_path_str, "Path to SQLite database")
      ->default_str(std::string(k_DefaultDb));
  app.add_option("--log-level", log_level, "Log level (trace/debug/info/warn/error)")
      ->default_str("info");

  // ── import-dat ─────────────────────────────────────────────
  auto* cmd_import = app.add_subcommand("import-dat", "Import a No-Intro DAT file");
  std::string import_path;
  cmd_import->add_option("path", import_path, "Path to .dat file")->required();

  // ── scan ───────────────────────────────────────────────────
  auto* cmd_scan = app.add_subcommand("scan", "Scan a directory for ROM files");
  std::string scan_dir;
  std::string scan_extensions;
  cmd_scan->add_option("directory", scan_dir, "Directory to scan")->required();
  cmd_scan->add_option("--extensions,-e", scan_extensions, "File extensions (comma-separated)");

  // ── verify ─────────────────────────────────────────────────
  auto* cmd_verify = app.add_subcommand("verify", "Match files and classify ROMs");
  std::string verify_system;
  cmd_verify->add_option("--system,-s", verify_system, "Filter by system name");

  // ── sync ───────────────────────────────────────────────────
  auto* cmd_sync = app.add_subcommand("sync", "Full pipeline: import → scan → verify");
  std::string sync_dat;
  std::string sync_dir;
  cmd_sync->add_option("dat", sync_dat, "Path to .dat file")->required();
  cmd_sync->add_option("directory", sync_dir, "ROM directory")->required();

  // ── report ─────────────────────────────────────────────────
  auto* cmd_report = app.add_subcommand("report", "Generate a collection report");
  std::string report_type = "summary";
  std::string report_format = "text";
  std::string report_system;
  cmd_report->add_option("type", report_type, "Report type (summary/missing)")
      ->default_str("summary");
  cmd_report->add_option("--format,-f", report_format, "Output format (text/csv/json)")
      ->default_str("text");
  cmd_report->add_option("--system,-s", report_system, "Filter by system name");

  // ── systems ────────────────────────────────────────────────
  auto* cmd_systems = app.add_subcommand("systems", "List all known systems");

  // ── status ─────────────────────────────────────────────────
  auto* cmd_status = app.add_subcommand("status", "Show database state summary");
  std::string status_system;
  cmd_status->add_option("--system,-s", status_system, "Filter by system name");

  app.require_subcommand(1);

  CLI11_PARSE(app, argc, argv);

  // Initialize logging
  romulus::core::init_logging(log_level);

  auto db_path = get_db_path(db_path_str);

  try {
    romulus::service::RomulusService svc(db_path);

    // ── import-dat ───────────────────────────────────────────
    if (cmd_import->parsed()) {
      auto result = svc.import_dat(import_path);
      if (!result) {
        std::cerr << "Error: " << result.error().message << "\n";
        return 1;
      }
      std::cout << "Imported DAT: " << result->name << " v" << result->version << "\n";
    }

    // ── scan ─────────────────────────────────────────────────
    else if (cmd_scan->parsed()) {
      auto ext =
          scan_extensions.empty() ? std::nullopt : std::optional<std::string>{scan_extensions};
      auto result = svc.scan_directory(scan_dir, ext);
      if (!result) {
        std::cerr << "Error: " << result.error().message << "\n";
        return 1;
      }
      std::cout << "Scan complete: " << result->files_scanned << " files, " << result->files_hashed
                << " hashed, " << result->files_skipped << " skipped, "
                << result->archives_processed << " archives\n";
    }

    // ── verify ───────────────────────────────────────────────
    else if (cmd_verify->parsed()) {
      auto sys = verify_system.empty() ? std::nullopt : std::optional{verify_system};
      auto result = svc.verify(sys);
      if (!result) {
        std::cerr << "Error: " << result.error().message << "\n";
        return 1;
      }
      std::cout << "Verification complete.\n";
    }

    // ── sync ─────────────────────────────────────────────────
    else if (cmd_sync->parsed()) {
      auto result = svc.full_sync(sync_dat, sync_dir);
      if (!result) {
        std::cerr << "Error: " << result.error().message << "\n";
        return 1;
      }
      std::cout << "Full sync complete.\n";

      // Print summary
      auto summary = svc.generate_report(romulus::core::ReportType::Summary,
                                         romulus::core::ReportFormat::Text);
      if (summary) {
        std::cout << *summary;
      }
    }

    // ── report ───────────────────────────────────────────────
    else if (cmd_report->parsed()) {
      auto type = romulus::core::ReportType::Summary;
      if (report_type == "missing") {
        type = romulus::core::ReportType::Missing;
      }

      auto fmt = romulus::core::ReportFormat::Text;
      if (report_format == "csv") {
        fmt = romulus::core::ReportFormat::Csv;
      } else if (report_format == "json") {
        fmt = romulus::core::ReportFormat::Json;
      }

      auto sys = report_system.empty() ? std::nullopt : std::optional{report_system};
      auto result = svc.generate_report(type, fmt, sys);
      if (!result) {
        std::cerr << "Error: " << result.error().message << "\n";
        return 1;
      }
      std::cout << *result;
    }

    // ── systems ──────────────────────────────────────────────
    else if (cmd_systems->parsed()) {
      auto result = svc.list_systems();
      if (!result) {
        std::cerr << "Error: " << result.error().message << "\n";
        return 1;
      }
      if (result->empty()) {
        std::cout << "No systems registered. Import a DAT file first.\n";
      } else {
        std::cout << "Known systems:\n";
        for (const auto& sys : *result) {
          std::cout << "  " << sys.name;
          if (!sys.short_name.empty()) {
            std::cout << " (" << sys.short_name << ")";
          }
          std::cout << "\n";
        }
      }
    }

    // ── status ───────────────────────────────────────────────
    else if (cmd_status->parsed()) {
      auto sys = status_system.empty() ? std::nullopt : std::optional{status_system};
      auto result = svc.generate_report(
          romulus::core::ReportType::Summary, romulus::core::ReportFormat::Text, sys);
      if (!result) {
        std::cerr << "Error: " << result.error().message << "\n";
        return 1;
      }
      std::cout << *result;
    }

  } catch (const std::exception& e) {
    std::cerr << "Fatal error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
