#include "romulus/scanner/rom_scanner.hpp"

#include "romulus/core/logging.hpp"
#include "romulus/scanner/archive_service.hpp"
#include "romulus/scanner/hash_service.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <ranges>
#include <thread>
#include <vector>

namespace romulus::scanner {

std::vector<std::string> RomScanner::get_default_extensions() {
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

bool RomScanner::matches_extension(const std::filesystem::path& path,
                                   const std::vector<std::string>& extensions) {
  auto ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return std::ranges::any_of(extensions, [&ext](const std::string& e) { return ext == e; });
}

Result<core::ScanResult> RomScanner::scan(
    const std::filesystem::path& directory,
    std::function<bool(std::string_view, std::int64_t, std::int64_t)> skip_check,
    std::optional<std::vector<std::string>> extensions) {
  if (!std::filesystem::exists(directory)) {
    return std::unexpected(core::Error{core::ErrorCode::DirectoryNotFound,
                                       "Scan directory not found: " + directory.string()});
  }

  auto ext_filter =
      (!extensions || extensions->empty()) ? get_default_extensions() : std::move(*extensions);

  ROMULUS_INFO("Scanning directory: {}", directory.string());

  // Phase 1: Collect all candidate files
  struct FileCandidate {
    std::filesystem::path path;
    std::int64_t size;
    std::int64_t last_write_time; ///< Filesystem mtime (Unix epoch seconds)
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
    // Convert filesystem mtime to Unix epoch seconds.
    const auto lwt = std::chrono::clock_cast<std::chrono::system_clock>(entry.last_write_time());
    const auto mtime =
        std::chrono::duration_cast<std::chrono::seconds>(lwt.time_since_epoch()).count();
    candidates.push_back({
        .path = entry.path(),
        .size = static_cast<std::int64_t>(entry.file_size()),
        .last_write_time = mtime,
        .is_archive = ArchiveService::is_archive(entry.path()),
    });
  }

  ROMULUS_INFO("Found {} candidate files", candidates.size());

  core::ScanReport report;
  std::atomic<std::int64_t> files_hashed{0};
  std::atomic<std::int64_t> files_skipped{0};
  std::mutex result_mutex;
  std::vector<core::ScannedROM> scanned_files;

  // Phase 2: Expand archives into individual entries
  struct HashJob {
    // Pre-computed virtual path used for the skip_check predicate before ScannedROM is created.
    // Mirrors the ScannedROM::virtual_path() format: "path" or "archive::entry".
    std::string virtual_path;
    std::int64_t size;
    std::int64_t last_write_time; ///< Mtime of the physical file (Unix epoch seconds)
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
            .virtual_path =
                candidate.path.string() + std::string(core::k_ArchiveEntrySeparator) + entry.name,
            .size = entry.size,
            .last_write_time = candidate.last_write_time,
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
          .last_write_time = candidate.last_write_time,
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

  // Reserve capacity for the results vector. jobs.size() is an upper bound since some
  // jobs may be skipped or fail to hash; this avoids reallocations under the mutex.
  scanned_files.reserve(jobs.size());

  auto process_job = [&](const HashJob& job) {
    // Check if already scanned via caller-supplied predicate.
    // The predicate receives the virtual path, current size, and physical file mtime so it can
    // skip re-hashing only when all three match the stored values. Called concurrently from
    // multiple worker threads — the predicate must be thread-safe for concurrent reads.
    if (skip_check && skip_check(job.virtual_path, job.size, job.last_write_time)) {
      ++files_skipped;
      return;
    }

    // Compute hashes
    auto digest = job.is_archive_entry
                      ? HashService::compute_hashes_archive(job.real_path, job.entry_index)
                      : HashService::compute_hashes(job.real_path);

    if (!digest) {
      ROMULUS_WARN("Hash failed for '{}': {}", job.virtual_path, digest.error().message);
      return;
    }

    // Collect ROM info — storage is the caller's responsibility
    const auto sha256_hex = digest->to_hex_sha256();
    core::ScannedROM scanned_rom{
        .archive_path = job.real_path,
        .entry_name =
            job.is_archive_entry ? std::optional<std::string>{job.entry_name} : std::nullopt,
        .size = job.size,
        .hash_digest = *digest,
        .last_write_time = job.last_write_time,
    };

    ROMULUS_DEBUG("Hashed '{}': SHA256={}", job.virtual_path, sha256_hex);

    {
      std::lock_guard lock(result_mutex);
      scanned_files.push_back(std::move(scanned_rom));
    }

    ++files_hashed;
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
  report.files_hashed = files_hashed.load();
  report.files_skipped = files_skipped.load();

  ROMULUS_INFO("Scan complete: {} files, {} hashed, {} skipped, {} archives",
               report.files_scanned,
               report.files_hashed,
               report.files_skipped,
               report.archives_processed);

  return core::ScanResult{.report = report, .files = std::move(scanned_files)};
}

} // namespace romulus::scanner
