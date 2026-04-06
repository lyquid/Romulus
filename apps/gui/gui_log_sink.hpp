#pragma once

/// @file gui_log_sink.hpp
/// @brief Thread-safe in-memory spdlog sink for displaying log messages in the GUI.

// ImGui headers are not needed here — this is a pure spdlog component.

#if defined(__GNUC__) || defined(__clang__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/base_sink.h>
#if defined(__GNUC__) || defined(__clang__)
  #pragma GCC diagnostic pop
#endif

#include <mutex>
#include <string>
#include <vector>

namespace romulus::gui {

/// Thread-safe spdlog sink that stores formatted log messages in a ring buffer.
/// Registered with the global logger in GuiApp to capture messages for the "Log" tab.
class GuiLogSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
  /// Maximum number of entries to keep (oldest are evicted when the buffer is full).
  static constexpr std::size_t k_MaxEntries = 1000;

  GuiLogSink() {
    // Use a compact pattern without color markers or source location, suitable for GUI display.
    set_formatter(std::make_unique<spdlog::pattern_formatter>(
        "[%H:%M:%S.%e] [%l] %v", spdlog::pattern_time_type::local, std::string{}));
  }

  /// Returns a snapshot of all stored log entries (thread-safe, copies the buffer).
  [[nodiscard]] auto get_entries() -> std::vector<std::string> {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_;
  }

  /// Clears all stored log entries.
  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
  }

protected:
  void sink_it_(const spdlog::details::log_msg& msg) override {
    // Called with mutex_ already held by base_sink::log().
    spdlog::memory_buf_t formatted;
    formatter_->format(msg, formatted);
    std::string entry(formatted.data(), formatted.size());

    // Strip trailing newline / carriage-return added by the formatter.
    while (!entry.empty() && (entry.back() == '\n' || entry.back() == '\r')) {
      entry.pop_back();
    }

    if (entries_.size() >= k_MaxEntries) {
      entries_.erase(entries_.begin());
    }
    entries_.push_back(std::move(entry));
  }

  void flush_() override {}

private:
  std::vector<std::string> entries_;
};

} // namespace romulus::gui
