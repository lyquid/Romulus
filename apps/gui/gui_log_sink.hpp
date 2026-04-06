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

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace romulus::gui {

/// A single captured log entry with its text and severity level.
struct LogEntry {
  std::string text;
  spdlog::level::level_enum level = spdlog::level::info;
};

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

  /// Returns a snapshot of all stored log entries when the buffer has changed since the last call.
  /// @param last_generation  The generation value from the previous call (use 0 on first call).
  /// @param[out] out_generation  Receives the current generation value regardless of whether the
  ///                             buffer changed. The caller should pass this back as
  ///                             `last_generation` on the next call.
  /// @return Entries snapshot, or std::nullopt if nothing has changed since last_generation.
  [[nodiscard]] auto get_entries_if_changed(std::uint64_t last_generation,
                                            std::uint64_t& out_generation)
      -> std::optional<std::vector<LogEntry>> {
    std::lock_guard<std::mutex> lock(mutex_);
    out_generation = generation_;
    if (generation_ == last_generation) {
      return std::nullopt;
    }
    return std::vector<LogEntry>(entries_.begin(), entries_.end());
  }

  /// Clears all stored log entries.
  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    ++generation_;
  }

protected:
  void sink_it_(const spdlog::details::log_msg& msg) override {
    // Called with mutex_ already held by base_sink::log().
    spdlog::memory_buf_t formatted;
    formatter_->format(msg, formatted);
    std::string text(formatted.data(), formatted.size());

    // Strip trailing newline / carriage-return added by the formatter.
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
      text.pop_back();
    }

    // std::deque provides O(1) front removal — no shifting of elements.
    if (entries_.size() >= k_MaxEntries) {
      entries_.pop_front();
    }
    entries_.push_back({std::move(text), msg.level});
    ++generation_;
  }

  void flush_() override {}

private:
  std::deque<LogEntry> entries_;
  std::uint64_t generation_ = 0;
};

} // namespace romulus::gui
