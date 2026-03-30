#pragma once

/// @file logging.hpp
/// @brief Logging facade wrapping spdlog for structured logging.

#include <spdlog/spdlog.h>

#include <memory>
#include <string_view>

namespace romulus::core {

/// Initializes the ROMULUS logger with the given verbosity level.
/// Must be called once at application startup.
/// @param level Log level: "trace", "debug", "info", "warn", "error", "critical"
void init_logging(std::string_view level = "info");

/// Returns the application-wide logger instance.
/// @return Shared pointer to the spdlog logger.
[[nodiscard]] auto get_logger() -> std::shared_ptr<spdlog::logger>&;

} // namespace romulus::core

// ── Convenience macros ───────────────────────────────────────
// Use these throughout ROMULUS instead of calling spdlog directly.
// They automatically route to the project logger.

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define ROMULUS_TRACE(...) SPDLOG_LOGGER_TRACE(::romulus::core::get_logger(), __VA_ARGS__)
#define ROMULUS_DEBUG(...) SPDLOG_LOGGER_DEBUG(::romulus::core::get_logger(), __VA_ARGS__)
#define ROMULUS_INFO(...) SPDLOG_LOGGER_INFO(::romulus::core::get_logger(), __VA_ARGS__)
#define ROMULUS_WARN(...) SPDLOG_LOGGER_WARN(::romulus::core::get_logger(), __VA_ARGS__)
#define ROMULUS_ERROR(...) SPDLOG_LOGGER_ERROR(::romulus::core::get_logger(), __VA_ARGS__)
#define ROMULUS_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(::romulus::core::get_logger(), __VA_ARGS__)
// NOLINTEND(cppcoreguidelines-macro-usage)
