#pragma once

/// @file gui_app.hpp
/// @brief Modular ImGui + GLFW GUI application for ROMULUS.
/// Decoupled from the core service — can be swapped for a web UI or disabled entirely.

#include "gui_log_sink.hpp"
#include "gui_logic.hpp"
#include "romulus/core/types.hpp"
#include "romulus/service/romulus_service.hpp"

#include <array>
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
  GuiApp& operator=(const GuiApp&) = delete;
  GuiApp(GuiApp&&) = delete;
  GuiApp& operator=(GuiApp&&) = delete;

  /// Runs the main render loop until the window is closed.
  void run();

private:
  // ── Initialization helpers ──────────────────────────────
  void init_glfw();
  void init_imgui();
  void shutdown();

  // ── UI panels ───────────────────────────────────────────
  void render_main_menu_bar();
  void render_dats_tab();
  void render_folders_tab();
  void render_db_tab();
  void render_log_panel();
  void render_status_bar();
  void render_toast();

  // ── Theme ────────────────────────────────────────────────
  static void apply_custom_theme();

  // ── Action handlers (launch background tasks) ──────────
  void action_import_dat();
  void action_add_rom_folder();
  void action_rescan_folders();
  void action_remove_folder(std::int64_t id);
  void action_check_dat();
  void action_verify();
  void action_purge_database();
  void action_delete_dat();

  // ── Background task management ─────────────────────────
  void check_pending_task();
  [[nodiscard]] bool is_busy() const;

  // ── Data refresh ────────────────────────────────────────
  void refresh_dat_versions();
  void refresh_folders();

  // ── Checklist sorting ──────────────────────────────────
  void apply_audit_sort();
  void apply_db_filter_sort();   ///< Recompute db_display_rows_ from current filter + sort
  void rebuild_db_lower_cache(); ///< Pre-compute lowercased cell strings for filter matching

  // ── Toast notification ─────────────────────────────────
  void show_toast(const std::string& message);

  // ── State ───────────────────────────────────────────────
  service::RomulusService& svc_;
  GLFWwindow* window_ = nullptr;
  std::shared_ptr<GuiLogSink> log_sink_;

  // Background task state
  struct PendingTask {
    std::future<std::string> result;
    bool refresh_dat_versions = false;
    bool refresh_checklist = false;
    bool refresh_folders = false;
  };

  // DB Explorer sort / filter / navigation
  static constexpr std::size_t k_DbMaxFilterLen = 256;

  /// Centralized mutable GUI state shared across all tab renderers.
  struct GuiState {
    // DAT selection
    std::vector<core::DatVersion> dat_versions; ///< All imported DAT versions
    int selected_dat_index = -1;                ///< Currently selected DAT index

    core::DatAudit dat_audit;
    bool dat_audit_loaded = false;
    DatAuditFilter dat_audit_filter = DatAuditFilter::All;
    int dat_audit_sort_col = 0;
    bool dat_audit_sort_ascending = true;
    bool scroll_dat_audit_top = false;

    std::vector<core::ScannedDirectory> scanned_dirs;
    std::string status_message;
    bool show_purge_confirm = false;
    bool show_delete_dat_confirm = false;

    std::optional<PendingTask> pending_task;

    std::string toast_message;
    float toast_timer = 0.0F;

    std::vector<LogEntry> log_entries_cache;
    std::uint64_t log_generation = 0;

    std::vector<std::string> db_table_names;
    int selected_db_table_index = -1;
    core::TableQueryResult db_table_data;
    bool db_tab_loaded = false;

    std::array<char, k_DbMaxFilterLen> db_filter_buf{};
    std::string db_filter_lower;
    int db_sort_col = -1;
    bool db_sort_ascending = true;
    bool db_view_dirty = true;
    bool scroll_db_top = false;
    bool scroll_db_bottom = false;
    std::vector<std::size_t> db_display_rows;
    std::vector<std::vector<std::string>> db_table_lower_rows;
  };

  GuiState state_{};

  // Compatibility aliases while preserving existing member names in implementation code.
  std::vector<core::DatVersion>& dat_versions_ = state_.dat_versions;
  int& selected_dat_index_ = state_.selected_dat_index;
  core::DatAudit& dat_audit_ = state_.dat_audit;
  bool& dat_audit_loaded_ = state_.dat_audit_loaded;
  DatAuditFilter& dat_audit_filter_ = state_.dat_audit_filter;
  int& dat_audit_sort_col_ = state_.dat_audit_sort_col;
  bool& dat_audit_sort_ascending_ = state_.dat_audit_sort_ascending;
  bool& scroll_dat_audit_top_ = state_.scroll_dat_audit_top;
  std::vector<core::ScannedDirectory>& scanned_dirs_ = state_.scanned_dirs;
  std::string& status_message_ = state_.status_message;
  bool& show_purge_confirm_ = state_.show_purge_confirm;
  bool& show_delete_dat_confirm_ = state_.show_delete_dat_confirm;
  std::optional<PendingTask>& pending_task_ = state_.pending_task;
  std::string& toast_message_ = state_.toast_message;
  float& toast_timer_ = state_.toast_timer;
  std::vector<LogEntry>& log_entries_cache_ = state_.log_entries_cache;
  std::uint64_t& log_generation_ = state_.log_generation;
  std::vector<std::string>& db_table_names_ = state_.db_table_names;
  int& selected_db_table_index_ = state_.selected_db_table_index;
  core::TableQueryResult& db_table_data_ = state_.db_table_data;
  bool& db_tab_loaded_ = state_.db_tab_loaded;
  std::array<char, k_DbMaxFilterLen>& db_filter_buf_ = state_.db_filter_buf;
  std::string& db_filter_lower_ = state_.db_filter_lower;
  int& db_sort_col_ = state_.db_sort_col;
  bool& db_sort_ascending_ = state_.db_sort_ascending;
  bool& db_view_dirty_ = state_.db_view_dirty;
  bool& scroll_db_top_ = state_.scroll_db_top;
  bool& scroll_db_bottom_ = state_.scroll_db_bottom;
  std::vector<std::size_t>& db_display_rows_ = state_.db_display_rows;
  std::vector<std::vector<std::string>>& db_table_lower_rows_ = state_.db_table_lower_rows;
};

} // namespace romulus::gui
