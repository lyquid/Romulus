#pragma once

/// @file gui_app.hpp
/// @brief Modular ImGui + GLFW GUI application for ROMULUS.
/// Decoupled from the core service — can be swapped for a web UI or disabled entirely.

#include "gui_log_sink.hpp"
#include "romulus/core/types.hpp"
#include "romulus/service/romulus_service.hpp"

#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>
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
  /// @param svc     Reference to the ROMULUS service (must outlive GuiApp).
  /// @param log_sink Pre-registered log sink that has already been added to the global
  ///                 logger (so early startup messages are also captured). Ownership
  ///                 is shared; GuiApp retains the sink only to read/clear entries.
  explicit GuiApp(service::RomulusService& svc, std::shared_ptr<GuiLogSink> log_sink);
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
  void render_rom_checklist_panel();
  void render_log_panel();
  void render_status_bar();
  void render_toast();

  // ── Action handlers (launch background tasks) ──────────
  void action_import_dat();
  void action_add_rom_folder();
  void action_rescan_folders();
  void action_check_dat();
  void action_verify();
  void action_purge_database();

  // ── Background task management ─────────────────────────
  void check_pending_task();
  [[nodiscard]] auto is_busy() const -> bool;

  // ── Data refresh ────────────────────────────────────────
  void refresh_dat_versions();

  // ── Checklist sorting ──────────────────────────────────
  void apply_checklist_sort();

  // ── Toast notification ─────────────────────────────────
  void show_toast(const std::string& message);

  // ── State ───────────────────────────────────────────────
  service::RomulusService& svc_;
  GLFWwindow* window_ = nullptr;
  std::shared_ptr<GuiLogSink> log_sink_;

  // DAT selection
  std::vector<core::DatVersion> dat_versions_; ///< All imported DAT versions
  int selected_dat_index_ = -1;                ///< Currently selected DAT index

  // ROM checklist
  struct RomChecklistEntry {
    std::string name;
    std::int64_t size = 0;
    std::string crc32;
    core::RomStatusType status = core::RomStatusType::Missing;
  };
  std::vector<RomChecklistEntry> rom_checklist_;
  int checklist_sort_col_ = -1;
  bool checklist_sort_ascending_ = true;

  // Scanned ROM directories (for rescan)
  std::vector<std::filesystem::path> scanned_dirs_;

  // Status
  std::string status_message_;

  // Confirmation dialog state
  bool show_purge_confirm_ = false;

  // Background task state
  struct PendingTask {
    std::future<std::string> result;
    bool refresh_dat_versions = false;
    bool refresh_checklist = false;
  };
  std::optional<PendingTask> pending_task_;

  // Toast notification state
  std::string toast_message_;
  float toast_timer_ = 0.0F;

  // Log panel cached state — updated only when the sink signals new content
  std::vector<LogEntry> log_entries_cache_;
  std::uint64_t log_generation_ = 0; ///< Last-seen generation from the log sink
};

} // namespace romulus::gui
