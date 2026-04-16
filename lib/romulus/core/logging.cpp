#include "romulus/core/logging.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <mutex>

namespace romulus::core {

namespace {
std::shared_ptr<spdlog::logger>
    g_logger; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
std::once_flag g_init_flag;
} // namespace

void init_logging(std::string_view level) {
  std::call_once(g_init_flag, [&level]() {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    g_logger = std::make_shared<spdlog::logger>("romulus", console_sink);

    g_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
    g_logger->set_level(spdlog::level::from_str(std::string(level)));

    spdlog::set_default_logger(g_logger);
  });
}

std::shared_ptr<spdlog::logger>& get_logger() {
  if (!g_logger) {
    init_logging();
  }
  return g_logger;
}

} // namespace romulus::core
