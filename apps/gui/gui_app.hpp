#pragma once

/// @file gui_app.hpp
/// @brief Modular ImGui + GLFW GUI application for ROMULUS.
/// Decoupled from the core service — can be swapped for a web UI or disabled entirely.

#include "gui_log_sink.hpp"
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

  // ── Background task management ─────────────────────────
  void check_pending_task();
  [[nodiscard]] auto is_busy() const -> bool;

  // ── Data refresh ────────────────────────────────────────
  void refresh_dat_versions();
  void refresh_folders();

  // ── Checklist sorting ──────────────────────────────────
  void apply_checklist_sort();
  void apply_game_sort();

  // ── Toast notification ─────────────────────────────────
  void show_toast(const std::string& message);

  // ── State ───────────────────────────────────────────────
  service::RomulusService& svc_;
  GLFWwindow* window_ = nullptr;
  std::shared_ptr<GuiLogSink> log_sink_;

  // DAT selection
  std::vector<core::DatVersion> dat_versions_; ///< All imported DAT versions
  int selected_dat_index_ = -1;                ///< Currently selected DAT index

  // ROM checklist — full flat list of all ROMs for the selected DAT.
  struct RomChecklistEntry {
    std::int64_t game_id = 0;           ///< FK to the owning game (used to filter per-game view)
    std::string name;
    std::string name_lower; ///< Lowercase copy of name — precomputed for filter matching
    std::int64_t size = 0;
    std::string sha1;
    std::string md5;
    std::string crc32;
    core::RomStatusType status = core::RomStatusType::Missing;
  };
  std::vector<RomChecklistEntry> rom_checklist_;
  // Default sort: ROM Name (column 1) ascending — matches ImGuiTableColumnFlags_DefaultSort on
  // that column.
  int checklist_sort_col_ = 1;
  bool checklist_sort_ascending_ = true;
  bool scroll_checklist_top_ = false;    ///< One-shot flag: scroll ROM detail table to top
  bool scroll_checklist_bottom_ = false; ///< One-shot flag: scroll ROM detail table to bottom

  // Game checklist — one entry per unique game in the selected DAT (left panel).
  struct GameChecklistEntry {
    std::int64_t game_id = 0;
    std::string name;
    std::string name_lower; ///< Lowercase copy for filter matching
    int rom_count = 0;      ///< Number of ROMs belonging to this game
    core::RomStatusType status = core::RomStatusType::Missing; ///< Aggregate across all ROMs
  };
  std::vector<GameChecklistEntry> game_checklist_;
  std::int64_t selected_game_id_ = -1; ///< game_id of the currently selected game, -1 = none
  int game_sort_col_ = 1;              ///< Default: sort by Game Name
  bool game_sort_ascending_ = true;

  // Precomputed status counters — recomputed once when the checklist is loaded,
  // not every frame, to avoid O(n) work in the render loop.
  struct ChecklistStats {
    std::int64_t total = 0;
    std::int64_t verified = 0;
    std::int64_t missing = 0;
    std::int64_t unverified = 0;
    std::int64_t mismatch = 0;
    std::int64_t games_total = 0; ///< Total number of unique games
  };
  ChecklistStats checklist_stats_;

  // Game panel filter state (left panel)
  static constexpr std::size_t k_MaxFilterLen = 256; ///< Max bytes for the name filter input
  std::array<char, k_MaxFilterLen> game_filter_buf_{};
  /// ASCII-lowercased copy of game_filter_buf_, recomputed only on edit.
  std::string game_filter_lower_;
  int game_status_filter_ = 0;       ///< 0=All, 1=Verified, 2=Missing, 3=Unverified, 4=Mismatch
  bool scroll_game_top_ = false;     ///< One-shot flag: scroll game table to the first row
  bool scroll_game_bottom_ = false;  ///< One-shot flag: scroll game table to the last row

  // Scanned ROM directories (persisted in DB)
  std::vector<core::ScannedDirectory> scanned_dirs_;

  // Status
  std::string status_message_;

  // Confirmation dialog state
  bool show_purge_confirm_ = false;

  // Background task state
  struct PendingTask {
    std::future<std::string> result;
    bool refresh_dat_versions = false;
    bool refresh_checklist = false;
    bool refresh_folders = false;
  };
  std::optional<PendingTask> pending_task_;

  // Toast notification state
  std::string toast_message_;
  float toast_timer_ = 0.0F;

  // Log panel cached state — updated only when the sink signals new content
  std::vector<LogEntry> log_entries_cache_;
  std::uint64_t log_generation_ = 0; ///< Last-seen generation from the log sink

  // DB Explorer tab state
  std::vector<std::string> db_table_names_;  ///< Table names loaded by "Read DB"
  int selected_db_table_index_ = -1;         ///< Currently selected table index
  core::TableQueryResult db_table_data_;     ///< Data for the currently selected table
  bool db_tab_loaded_ = false;               ///< True once "Read DB" has been invoked
};

} // namespace romulus::gui
