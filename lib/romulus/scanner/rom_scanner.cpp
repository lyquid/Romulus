#include "romulus/scanner/rom_scanner.hpp"

#include "romulus/core/logging.hpp"
#include "romulus/database/database.hpp"
#include "romulus/scanner/archive_service.hpp"
#include "romulus/scanner/hash_service.hpp"

#include <algorithm>
#include <mutex>
#include <ranges>
#include <sstream>
#include <thread>
#include <vector>

namespace romulus::scanner {

namespace {

auto split_extensions(const std::string& ext_str) -> std::vector<std::string> {
  std::vector<std::string> exts;
  std::istringstream stream(ext_str);
  std::string token;
  while (std::getline(stream, token, ',')) {
    // Trim and ensure leading dot
    auto trimmed = token;
    while (!trimmed.empty() && trimmed.front() == ' ') {
      trimmed.erase(trimmed.begin());
    }
    if (!trimmed.empty() && trimmed.front() != '.') {
      trimmed = "." + trimmed;
    }
    std::transform(trimmed.begin(), trimmed.end(), trimmed.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    if (!trimmed.empty()) {
      exts.push_back(trimmed);
    }
  }
  return exts;
}

} // namespace

auto RomScanner::get_default_extensions() -> std::vector<std::string> {
  return {
      // Nintendo
      ".nes",
      ".smc",
      ".sfc",
      ".gb",
      ".gbc",
      ".gba",
      ".nds",
      ".n64",
      ".z64",
      ".v64",
      // Sega
      ".md",
      ".gen",
      ".sms",
      ".gg",
      ".32x",
      // Others
      ".pce",
      ".ngp",
      ".ngc",
      ".ws",
      ".wsc",
      // Generic
      ".bin",
      ".rom",
      // Archives
      ".zip",
      ".7z",
  };
}

auto RomScanner::matches_extension(const std::filesystem::path& path,
                                   const std::vector<std::string>& extensions) -> bool {
  auto ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return std::ranges::any_of(extensions, [&ext](const std::string& e) { return ext == e; });
}

auto RomScanner::scan(const std::filesystem::path& directory,
                      database::Database& db,
                      std::optional<std::string> extensions) -> Result<core::ScanReport> {
  if (!std::filesystem::exists(directory)) {
    return std::unexpected(core::Error{core::ErrorCode::DirectoryNotFound,
                                       "Scan directory not found: " + directory.string()});
  }

  auto ext_filter =
      extensions.has_value() ? split_extensions(*extensions) : get_default_extensions();

  ROMULUS_INFO("Scanning directory: {}", directory.string());

  // Phase 1: Collect all candidate files
  struct FileCandidate {
    std::filesystem::path path;
    std::int64_t size;
    bool is_archive;
  };

  std::vector<FileCandidate> candidates;

  for (const auto& entry : std::filesystem::recursive_directory_iterator(
           directory, std::filesystem::directory_options::skip_permission_denied)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    if (!matches_extension(entry.path(), ext_filter)) {
      continue;
    }
    candidates.push_back({
        .path = entry.path(),
        .size = static_cast<std::int64_t>(entry.file_size()),
        .is_archive = ArchiveService::is_archive(entry.path()),
    });
  }

  ROMULUS_INFO("Found {} candidate files", candidates.size());

  core::ScanReport report;
  std::mutex db_mutex;

  // Phase 2: Expand archives into individual entries
  struct HashJob {
    std::string virtual_path; // path or archive::entry
    std::int64_t size;
    std::filesystem::path real_path;
    std::string entry_name;        // display name; empty for regular files
    std::size_t entry_index;       // stable archive index; only valid when is_archive_entry
    bool is_archive_entry = false; // true when this job represents an entry inside an archive
  };

  std::vector<HashJob> jobs;

  for (const auto& candidate : candidates) {
    if (candidate.is_archive) {
      auto entries = ArchiveService::list_entries(candidate.path);
      if (!entries) {
        ROMULUS_WARN(
            "Failed to read archive '{}': {}", candidate.path.string(), entries.error().message);
        continue;
      }
      for (const auto& entry : *entries) {
        jobs.push_back({
            .virtual_path = candidate.path.string() + "::" + entry.name,
            .size = entry.size,
            .real_path = candidate.path,
            .entry_name = entry.name,
            .entry_index = entry.index,
            .is_archive_entry = true,
        });
      }
      ++report.archives_processed;
    } else {
      jobs.push_back({
          .virtual_path = candidate.path.string(),
          .size = candidate.size,
          .real_path = candidate.path,
          .entry_name = {},
          .entry_index = 0,
          .is_archive_entry = false,
      });
    }
  }

  ROMULUS_INFO("Total hash jobs: {} (including archive entries)", jobs.size());

  // Phase 3: Hash files in parallel
  auto num_threads = std::max(1u, std::thread::hardware_concurrency());
  ROMULUS_INFO("Hashing with {} threads", num_threads);

  auto process_job = [&](const HashJob& job) {
    // Check if already scanned
    {
      std::lock_guard lock(db_mutex);
      auto existing = db.find_file_by_path(job.virtual_path);
      if (existing && existing->has_value()) {
        ++report.files_skipped;
        return;
      }
    }

    // Compute hashes
    auto digest = job.is_archive_entry
                      ? HashService::compute_hashes_archive(job.real_path, job.entry_index)
                      : HashService::compute_hashes(job.real_path);

    if (!digest) {
      ROMULUS_WARN("Hash failed for '{}': {}", job.virtual_path, digest.error().message);
      return;
    }

    // Store in DB
    const auto sha256_hex = digest->to_hex_sha256();
    core::FileInfo file_info{
        .filename = job.is_archive_entry ? job.entry_name : job.real_path.filename().string(),
        .path = job.virtual_path,
        .size = job.size,
        .crc32 = digest->to_hex_crc32(),
        .md5 = digest->to_hex_md5(),
        .sha1 = digest->to_hex_sha1(),
        .sha256 = sha256_hex,
        .last_scanned = {},
        .is_archive_entry = job.is_archive_entry,
    };

    ROMULUS_DEBUG("Hashed '{}': SHA256={}", job.virtual_path, sha256_hex);

    {
      std::lock_guard lock(db_mutex);
      auto result = db.upsert_file(file_info);
      if (!result) {
        ROMULUS_WARN("DB insert failed for '{}': {}", job.virtual_path, result.error().message);
        return;
      }
    }

    ++report.files_hashed;
  };

  // Simple thread pool using jthread
  std::vector<std::jthread> threads;
  std::atomic<std::size_t> job_index{0};

  for (unsigned int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&]() {
      while (true) {
        auto idx = job_index.fetch_add(1);
        if (idx >= jobs.size()) {
          break;
        }
        process_job(jobs[idx]);
      }
    });
  }

  // jthreads auto-join on destruction
  threads.clear();

  report.files_scanned = static_cast<std::int64_t>(jobs.size());

  ROMULUS_INFO("Scan complete: {} files, {} hashed, {} skipped, {} archives",
               report.files_scanned,
               report.files_hashed,
               report.files_skipped,
               report.archives_processed);

  return report;
}

} // namespace romulus::scanner
