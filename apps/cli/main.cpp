#include "romulus/core/logging.hpp"
#include "romulus/core/types.hpp"
#include "romulus/scanner/archive_service.hpp"
#include "romulus/service/romulus_service.hpp"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <print>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr std::string_view k_Version = "0.1.0";
constexpr std::string_view k_DefaultDb = "romulus.db";

std::filesystem::path get_executable_dir(const char* argv0) {
  std::error_code error;
  auto absolute_path = std::filesystem::absolute(argv0, error);
  if (error) {
    return std::filesystem::current_path();
  }

  auto canonical_path = std::filesystem::weakly_canonical(absolute_path, error);
  if (error) {
    return absolute_path.parent_path();
  }

  return canonical_path.parent_path();
}

std::filesystem::path get_db_path(const std::string& db_flag,
                                  const std::filesystem::path& executable_dir) {
  if (!db_flag.empty()) {
    return db_flag;
  }
  // Default: next to the executable
  return executable_dir / k_DefaultDb;
}

std::string to_lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

/// Normalizes a single extension token: trims leading/trailing whitespace
/// (spaces, tabs, newlines, etc.), ensures a leading dot, and lowercases the result.
std::string normalize_extension(std::string ext) {
  constexpr std::string_view k_Whitespace = " \t\n\r\f\v";
  const auto first = ext.find_first_not_of(k_Whitespace);
  if (first == std::string::npos) {
    return {};
  }
  const auto last = ext.find_last_not_of(k_Whitespace);
  ext = ext.substr(first, last - first + 1);
  if (ext.front() != '.') {
    ext = "." + ext;
  }
  return to_lower(std::move(ext));
}

/// Normalizes a list of raw extension tokens (in-place).
std::vector<std::string> normalize_extensions(std::vector<std::string> exts) {
  for (auto& ext : exts) {
    ext = normalize_extension(std::move(ext));
  }
  std::erase_if(exts, [](const std::string& e) { return e.empty() || e == "."; });
  return exts;
}

bool is_dat_candidate(const std::filesystem::path& path) {
  const auto extension = to_lower(path.extension().string());
  return extension == ".dat" || extension == ".xml" ||
         romulus::scanner::ArchiveService::is_archive(path);
}

std::string describe_candidates(const std::vector<std::filesystem::path>& paths) {
  std::ostringstream stream;
  for (std::size_t index = 0; index < paths.size(); ++index) {
    if (index > 0) {
      stream << ", ";
    }
    stream << paths.at(index).filename().string();
  }
  return stream.str();
}

romulus::core::Result<std::filesystem::path> resolve_bundled_dat_path(
    const std::filesystem::path& executable_dir) {
  const auto dats_dir = executable_dir / "dats";
  if (!std::filesystem::exists(dats_dir) || !std::filesystem::is_directory(dats_dir)) {
    return std::unexpected(
        romulus::core::Error{romulus::core::ErrorCode::DirectoryNotFound,
                             "Bundled DAT directory not found: " + dats_dir.string()});
  }

  std::vector<std::filesystem::path> candidates;
  for (const auto& entry : std::filesystem::directory_iterator(dats_dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    if (is_dat_candidate(entry.path())) {
      candidates.push_back(entry.path());
    }
  }

  if (candidates.empty()) {
    return std::unexpected(
        romulus::core::Error{romulus::core::ErrorCode::FileNotFound,
                             "No bundled DAT artifacts found in: " + dats_dir.string()});
  }

  if (candidates.size() > 1) {
    return std::unexpected(romulus::core::Error{romulus::core::ErrorCode::InvalidArgument,
                                                "Multiple bundled DAT artifacts found in '" +
                                                    dats_dir.string() +
                                                    "': " + describe_candidates(candidates)});
  }

  return candidates.front();
}

romulus::core::Result<std::filesystem::path> resolve_import_path(
    const std::string& import_path, const std::filesystem::path& executable_dir) {
  if (!import_path.empty()) {
    return std::filesystem::path{import_path};
  }
  return resolve_bundled_dat_path(executable_dir);
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
  cmd_import->add_option("path", import_path, "Path to DAT or DAT archive");

  // ── scan ───────────────────────────────────────────────────
  auto* cmd_scan = app.add_subcommand("scan", "Scan a directory for ROM files");
  std::string scan_dir;
  std::vector<std::string> scan_extensions;
  cmd_scan->add_option("directory", scan_dir, "Directory to scan")->required();
  cmd_scan
      ->add_option(
          "--extensions,-e", scan_extensions, "File extensions (comma-separated, e.g. .nes,.gb)")
      ->delimiter(',');

  // ── verify ─────────────────────────────────────────────────
  auto* cmd_verify = app.add_subcommand("verify", "Match files and classify ROMs");
  std::string verify_dat;
  cmd_verify->add_option("--dat,-d", verify_dat, "Filter by DAT name");

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
  std::string report_dat;
  cmd_report->add_option("type", report_type, "Report type (summary/missing/duplicates/unverified)")
      ->default_str("summary");
  cmd_report->add_option("--format,-f", report_format, "Output format (text/csv/json)")
      ->default_str("text");
  cmd_report->add_option("--dat,-d", report_dat, "Filter by DAT name");

  // ── dats ───────────────────────────────────────────────────
  auto* cmd_dats = app.add_subcommand("dats", "List all imported DAT versions");

  // ── status ─────────────────────────────────────────────────
  auto* cmd_status = app.add_subcommand("status", "Show database state summary");
  std::string status_dat;
  cmd_status->add_option("--dat,-d", status_dat, "Filter by DAT name");

  app.require_subcommand(1);

  CLI11_PARSE(app, argc, argv);

  // Initialize logging
  romulus::core::init_logging(log_level);

  const auto executable_dir = get_executable_dir(argv[0]);
  auto db_path = get_db_path(db_path_str, executable_dir);

  try {
    romulus::service::RomulusService svc(db_path);

    // ── import-dat ───────────────────────────────────────────
    if (cmd_import->parsed()) {
      auto resolved_path = resolve_import_path(import_path, executable_dir);
      if (!resolved_path) {
        std::print(stderr, "Error: {}\n", resolved_path.error().message);
        return 1;
      }

      auto result = svc.import_dat(*resolved_path);
      if (!result) {
        std::print(stderr, "Error: {}\n", result.error().message);
        return 1;
      }
      std::print("Imported DAT: {} v{}\n", result->name, result->version);
    }

    // ── scan ─────────────────────────────────────────────────
    else if (cmd_scan->parsed()) {
      auto normalized = normalize_extensions(std::move(scan_extensions));
      auto ext = normalized.empty()
                     ? std::nullopt
                     : std::optional<std::vector<std::string>>{std::move(normalized)};
      auto result = svc.scan_directory(scan_dir, std::move(ext));
      if (!result) {
        std::print(stderr, "Error: {}\n", result.error().message);
        return 1;
      }
      std::print("Scan complete: {} files, {} hashed, {} skipped, {} archives\n",
                 result->files_scanned,
                 result->files_hashed,
                 result->files_skipped,
                 result->archives_processed);
    }

    // ── verify ───────────────────────────────────────────────
    else if (cmd_verify->parsed()) {
      auto dat = verify_dat.empty() ? std::nullopt : std::optional{verify_dat};
      auto result = svc.verify(dat);
      if (!result) {
        std::print(stderr, "Error: {}\n", result.error().message);
        return 1;
      }
      std::print("Verification complete.\n");
    }

    // ── sync ─────────────────────────────────────────────────
    else if (cmd_sync->parsed()) {
      auto result = svc.full_sync(sync_dat, sync_dir);
      if (!result) {
        std::print(stderr, "Error: {}\n", result.error().message);
        return 1;
      }
      std::print("Full sync complete.\n");

      // Print summary
      auto summary = svc.generate_report(romulus::core::ReportType::Summary,
                                         romulus::core::ReportFormat::Text);
      if (summary) {
        std::print("{}", *summary);
      }
    }

    // ── report ───────────────────────────────────────────────
    else if (cmd_report->parsed()) {
      auto type = romulus::core::ReportType::Summary;
      if (report_type == "missing") {
        type = romulus::core::ReportType::Missing;
      } else if (report_type == "duplicates") {
        type = romulus::core::ReportType::Duplicates;
      } else if (report_type == "unverified") {
        type = romulus::core::ReportType::Unverified;
      }

      auto fmt = romulus::core::ReportFormat::Text;
      if (report_format == "csv") {
        fmt = romulus::core::ReportFormat::Csv;
      } else if (report_format == "json") {
        fmt = romulus::core::ReportFormat::Json;
      }

      auto dat = report_dat.empty() ? std::nullopt : std::optional{report_dat};
      auto result = svc.generate_report(type, fmt, dat);
      if (!result) {
        std::print(stderr, "Error: {}\n", result.error().message);
        return 1;
      }
      std::print("{}", *result);
    }

    // ── dats ─────────────────────────────────────────────────
    else if (cmd_dats->parsed()) {
      auto result = svc.list_dat_versions();
      if (!result) {
        std::print(stderr, "Error: {}\n", result.error().message);
        return 1;
      }
      if (result->empty()) {
        std::print("No DAT versions imported. Use 'import-dat' first.\n");
      } else {
        std::print("Imported DATs:\n");
        for (const auto& dv : *result) {
          std::print("  {} v{} (imported: {})\n", dv.name, dv.version, dv.imported_at);
        }
      }
    }

    // ── status ───────────────────────────────────────────────
    else if (cmd_status->parsed()) {
      auto dat = status_dat.empty() ? std::nullopt : std::optional{status_dat};
      auto result = svc.generate_report(
          romulus::core::ReportType::Summary, romulus::core::ReportFormat::Text, dat);
      if (!result) {
        std::print(stderr, "Error: {}\n", result.error().message);
        return 1;
      }
      std::print("{}", *result);
    }

  } catch (const std::exception& e) {
    std::print(stderr, "Fatal error: {}\n", e.what());
    return 1;
  }

  return 0;
}
