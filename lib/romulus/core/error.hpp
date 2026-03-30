#pragma once

/// @file error.hpp
/// @brief Error types and Result alias using std::expected.

#include <expected>
#include <string>

namespace romulus::core {

/// Error categories for ROMULUS operations.
enum class ErrorCode {
  // General
  Unknown,
  InvalidArgument,
  NotFound,
  AlreadyExists,

  // Filesystem
  FileNotFound,
  FileReadError,
  FileWriteError,
  DirectoryNotFound,
  PermissionDenied,

  // DAT parsing
  DatParseError,
  DatInvalidFormat,
  DatVersionConflict,

  // Database
  DatabaseOpenError,
  DatabaseQueryError,
  DatabaseMigrationError,
  DatabaseTransactionError,

  // Hashing
  HashComputeError,

  // Archive
  ArchiveOpenError,
  ArchiveReadError,
  ArchiveUnsupportedFormat,

  // Matching / Classification
  MatchError,
  ClassificationError,

  // Network (future)
  NetworkError,
  DownloadError,
};

/// Structured error with code and human-readable message.
struct Error {
  ErrorCode code;
  std::string message;

  Error(ErrorCode c, std::string msg) : code(c), message(std::move(msg)) {}
};

/// Result type alias — the standard return type for all fallible operations.
/// Usage: `Result<DatFile>`, `Result<void>`, `Result<HashDigest>`
template <typename T> using Result = std::expected<T, Error>;

} // namespace romulus::core
