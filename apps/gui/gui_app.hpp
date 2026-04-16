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

  // ── Background task management ─────────────────────────
  void check_pending_task();
  [[nodiscard]] bool is_busy() const;

  // ── Data refresh ────────────────────────────────────────
  void refresh_dat_versions();
  void refresh_folders();

  // ── Checklist sorting ──────────────────────────────────
  void apply_checklist_sort();
  void apply_game_sort();
  void apply_db_filter_sort();   ///< Recompute db_display_rows_ from current filter + sort
  void rebuild_db_lower_cache(); ///< Pre-compute lowercased cell strings for filter matching

  // ── Toast notification ─────────────────────────────────
  void show_toast(const std::string& message);

  // ── State ───────────────────────────────────────────────
  service::RomulusService& svc_;
  GLFWwindow* window_ = nullptr;
  std::shared_ptr<GuiLogSink> log_sink_;

  // ROM checklist — full flat list of all ROMs for the selected DAT.
  struct RomChecklistEntry {
    std::int64_t game_id = 0; ///< FK to the owning game (used to filter per-game view)
    std::string name;
    std::string name_lower; ///< Lowercase copy of name — precomputed for filter matching
    std::int64_t size = 0;
    std::string sha1;
    std::string md5;
    std::string crc32;
    core::RomStatusType status = core::RomStatusType::Missing;
  };

  // Game checklist — one entry per unique game in the selected DAT (left panel).
  struct GameChecklistEntry {
    std::int64_t game_id = 0;
    std::string name;
    std::string name_lower; ///< Lowercase copy for filter matching
    int rom_count = 0;      ///< Number of ROMs belonging to this game
    core::RomStatusType status = core::RomStatusType::Missing; ///< Aggregate across all ROMs
  };

  // Per-game ROM index cache — indices into rom_checklist_ for the selected game.
  // Rebuilt whenever selected_game_id_ changes or rom_checklist_ is re-sorted/reloaded.
  static constexpr std::int64_t k_NoCachedGameId = -2;        ///< Sentinel: cache is stale
  static constexpr std::uint64_t k_InvalidGeneration = ~0ULL; ///< Sentinel: no generation cached

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

  // Game panel filter state (left panel)
  static constexpr std::size_t k_MaxFilterLen = 256; ///< Max bytes for the name filter input

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

    std::vector<RomChecklistEntry> rom_checklist;
    int checklist_sort_col = 1;
    bool checklist_sort_ascending = true;
    bool scroll_checklist_top = false;
    bool scroll_checklist_bottom = false;

    std::vector<GameChecklistEntry> game_checklist;
    std::int64_t selected_game_id = -1;
    int game_sort_col = 1;
    bool game_sort_ascending = true;

    std::vector<std::size_t> selected_rom_indices;
    std::int64_t cached_rom_game_id = k_NoCachedGameId;
    std::uint64_t rom_checklist_generation = 0;
    std::uint64_t cached_rom_generation = k_InvalidGeneration;

    ChecklistStats checklist_stats;
    std::array<char, k_MaxFilterLen> game_filter_buf{};
    std::string game_filter_lower;
    int game_status_filter = 0;
    bool scroll_game_top = false;
    bool scroll_game_bottom = false;

    std::vector<core::ScannedDirectory> scanned_dirs;
    std::string status_message;
    bool show_purge_confirm = false;

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
  std::vector<RomChecklistEntry>& rom_checklist_ = state_.rom_checklist;
  int& checklist_sort_col_ = state_.checklist_sort_col;
  bool& checklist_sort_ascending_ = state_.checklist_sort_ascending;
  bool& scroll_checklist_top_ = state_.scroll_checklist_top;
  bool& scroll_checklist_bottom_ = state_.scroll_checklist_bottom;
  std::vector<GameChecklistEntry>& game_checklist_ = state_.game_checklist;
  std::int64_t& selected_game_id_ = state_.selected_game_id;
  int& game_sort_col_ = state_.game_sort_col;
  bool& game_sort_ascending_ = state_.game_sort_ascending;
  std::vector<std::size_t>& selected_rom_indices_ = state_.selected_rom_indices;
  std::int64_t& cached_rom_game_id_ = state_.cached_rom_game_id;
  std::uint64_t& rom_checklist_generation_ = state_.rom_checklist_generation;
  std::uint64_t& cached_rom_generation_ = state_.cached_rom_generation;
  ChecklistStats& checklist_stats_ = state_.checklist_stats;
  std::array<char, k_MaxFilterLen>& game_filter_buf_ = state_.game_filter_buf;
  std::string& game_filter_lower_ = state_.game_filter_lower;
  int& game_status_filter_ = state_.game_status_filter;
  bool& scroll_game_top_ = state_.scroll_game_top;
  bool& scroll_game_bottom_ = state_.scroll_game_bottom;
  std::vector<core::ScannedDirectory>& scanned_dirs_ = state_.scanned_dirs;
  std::string& status_message_ = state_.status_message;
  bool& show_purge_confirm_ = state_.show_purge_confirm;
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
