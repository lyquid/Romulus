#pragma once

/// @file gui_app.hpp
/// @brief Modular ImGui + GLFW GUI application for ROMULUS.
/// Decoupled from the core service — can be swapped for a web UI or disabled entirely.

#include "romulus/core/types.hpp"
#include "romulus/service/romulus_service.hpp"

#include <filesystem>
#include <string>
#include <vector>

struct GLFWwindow;

namespace romulus::gui {

/// Self-contained ImGui + GLFW window that drives the ROMULUS GUI.
/// Owns the GLFW window and ImGui context; the service is injected.
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
  void render_summary_panel();
  void render_status_bar();

  // ── Action handlers ─────────────────────────────────────
  void action_import_dat();
  void action_scan_folder();
  void action_verify();
  void action_purge_database();

  // ── Data refresh ────────────────────────────────────────
  void refresh_files();
  void refresh_summary();

  // ── State ───────────────────────────────────────────────
  service::RomulusService& svc_;
  GLFWwindow* window_ = nullptr;

  // UI input buffers
  std::string dat_path_buf_;
  std::string scan_dir_buf_;

  // Cached data
  std::vector<core::FileInfo> files_;
  core::CollectionSummary summary_{};
  std::string status_message_;
  bool has_summary_ = false;

  // Confirmation dialog state
  bool show_purge_confirm_ = false;
};

} // namespace romulus::gui
