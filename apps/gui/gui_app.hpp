#pragma once

/// @file gui_app.hpp
/// @brief Modular ImGui + GLFW GUI application for ROMULUS.
/// Decoupled from the core service — can be swapped for a web UI or disabled entirely.

#include "gui_log_sink.hpp"
#include "romulus/core/types.hpp"
#include "romulus/service/romulus_service.hpp"

#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace romulus::gui {

/// Self-contained ImGui + GLFW window that drives the ROMULUS GUI.
/// Owns the GLFW window and ImGui context; the service is injected.
/// Long-running operations (scan, import, verify) run on a background thread
/// to keep the UI responsive.
class GuiApp final {
public:
  /// Initializes GLFW, creates a window, and sets up ImGui.
  /// @param svc Reference to the ROMULUS service (must outlive GuiApp).
  explicit GuiApp(service::RomulusService& svc);
  ~GuiApp();

  GuiApp(const GuiApp&) = delete;
  auto operator=(const GuiApp&) -> GuiApp& = delete;
  GuiApp(GuiApp&&) = delete;
  auto operator=(GuiApp&&) -> GuiApp& = delete;

  /// Runs the main render loop until the window is closed.
  void run();

private:
  // ── Initialization helpers ──────────────────────────────
  void init_glfw();
  void init_imgui();
  void shutdown();

  // ── UI panels ───────────────────────────────────────────
  void render_main_menu_bar();
  void render_actions_panel();
  void render_files_panel();
  void render_systems_panel();
  void render_summary_panel();
  void render_log_panel();
  void render_status_bar();
  void render_toast();

  // ── Hash cell rendering (right-click to copy) ──────────
  void render_hash_cell(int column, const std::string& hash, const char* label);

  // ── Table sorting ────────────────────────────────────────
  /// Sorts files_ in place according to the current sort_col_ / sort_ascending_ state.
  void apply_sort();

  // ── Action handlers (launch background tasks) ──────────
  void action_import_dat();
  void action_scan_folder();
  void action_verify();
  void action_purge_database();

  // ── Background task management ─────────────────────────
  void check_pending_task();
  [[nodiscard]] auto is_busy() const -> bool;

  // ── Data refresh ────────────────────────────────────────
  void refresh_files();
  void refresh_systems();
  void refresh_summary();

  // ── Toast notification ─────────────────────────────────
  void show_toast(const std::string& message);

  // ── State ───────────────────────────────────────────────
  service::RomulusService& svc_;
  GLFWwindow* window_ = nullptr;
  std::shared_ptr<GuiLogSink> log_sink_;

  // UI input buffers
  std::string dat_path_buf_;
  std::string scan_dir_buf_;

  // Cached data
  std::vector<core::FileInfo> files_;
  std::vector<core::SystemInfo> systems_;
  core::CollectionSummary summary_{};
  std::string status_message_;
  bool has_summary_ = false;

  // Confirmation dialog state
  bool show_purge_confirm_ = false;

  // Background task state
  struct PendingTask {
    std::future<std::string> result;
    bool refresh_files = false;
    bool refresh_summary = false;
    bool refresh_systems = false;
  };
  std::optional<PendingTask> pending_task_;

  // Toast notification state
  std::string toast_message_;
  float toast_timer_ = 0.0F;

  // Files table sort state
  int sort_col_ = -1;          ///< Active sort column index (-1 = unsorted)
  bool sort_ascending_ = true; ///< true = ascending, false = descending
};

} // namespace romulus::gui
