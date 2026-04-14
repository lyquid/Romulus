#include "gui_app.hpp"

#include "gui_app_shared.hpp"

// ImGui and backends — included as system headers to avoid third-party warnings.
// The SYSTEM include paths are set in CMakeLists.txt.
#if defined(__GNUC__) || defined(__clang__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
#include <imgui.h>
#if defined(__GNUC__) || defined(__clang__)
  #pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <charconv>
#include <cmath>
#include <ranges>
#include <string>

namespace romulus::gui {

void GuiApp::render_log_panel() {
  // Only copy the sink's buffer when new entries have been added.
  bool had_new_entries = false;
  if (auto new_entries = log_sink_->get_entries_if_changed(log_generation_, log_generation_)) {
    log_entries_cache_ = std::move(*new_entries);
    had_new_entries = true;
  }

  ImGui::Text("Log (%zu entries)", log_entries_cache_.size());
  ImGui::SameLine();
  if (ImGui::SmallButton("Clear")) {
    log_sink_->clear();
    log_entries_cache_.clear();
    log_generation_ = 0;
  }

  // Reserve space at the bottom for the horizontal scrollbar.
  constexpr float k_ScrollbarReserve = 30.0F;
  if (ImGui::BeginChild("##log_scroll",
                        ImVec2(0, -k_ScrollbarReserve),
                        false,
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    // Capture the at-bottom state *before* rendering new lines.
    const bool was_at_bottom = ImGui::GetScrollY() >= (ImGui::GetScrollMaxY() - 1.0F);

    for (const auto& entry : log_entries_cache_) {
      ImVec4 color = k_ColorLogDefault;
      if (entry.level == spdlog::level::warn) {
        color = k_ColorLogWarn;
      } else if (entry.level >= spdlog::level::err) {
        color = k_ColorLogError;
      } else if (entry.level <= spdlog::level::debug) {
        color = k_ColorLogDebug;
      }
      ImGui::TextColored(color, "%s", entry.text.c_str());
    }

    // Scroll to the bottom only when new entries arrived and the user hadn't scrolled up.
    if (had_new_entries && was_at_bottom) {
      ImGui::SetScrollHereY(1.0F);
    }
  }
  ImGui::EndChild();
}

} // namespace romulus::gui
