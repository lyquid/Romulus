# ── romulus_gui ────────────────────────────────────────────────
# Modular ImGui + GLFW GUI for ROMULUS.
# Only built when ROMULUS_ENABLE_GUI is ON.
# Dependencies: imgui, glfw3, OpenGL

# ── Find dependencies ─────────────────────────────────────────
# Try vcpkg imgui first, fall back to system pkg-config / manual
find_package(imgui CONFIG QUIET)
find_package(glfw3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

# ── ImGui backend sources ─────────────────────────────────────
# ImGui backends are header+source that must be compiled into the
# application. The system package ships them alongside the headers.
# vcpkg imgui bundles them into the imgui target.

set(IMGUI_BACKEND_SOURCES "")
if(NOT imgui_FOUND)
    # System package: compile backends from installed examples/doc
    find_path(IMGUI_BACKENDS_SRC_DIR
        NAMES imgui_impl_glfw.cpp
        PATHS
            /usr/share/doc/libimgui-dev/examples/backends
            /usr/local/share/doc/libimgui-dev/examples/backends
        NO_DEFAULT_PATH
    )
    if(NOT IMGUI_BACKENDS_SRC_DIR)
        message(FATAL_ERROR "ImGui backend sources not found. Install libimgui-dev or use vcpkg.")
    endif()
    set(IMGUI_BACKEND_SOURCES
        ${IMGUI_BACKENDS_SRC_DIR}/imgui_impl_glfw.cpp
        ${IMGUI_BACKENDS_SRC_DIR}/imgui_impl_opengl3.cpp
    )
endif()

# ── GUI executable ─────────────────────────────────────────────
add_executable(romulus_gui
    main.cpp
    gui_app.cpp
    ${IMGUI_BACKEND_SOURCES}
)

target_compile_features(romulus_gui PRIVATE cxx_std_23)

# ── Include directories ───────────────────────────────────────
target_include_directories(romulus_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT imgui_FOUND)
    # System imgui: headers in /usr/include/imgui, backends in /usr/include/imgui/backends
    find_path(IMGUI_INCLUDE_DIR NAMES imgui.h PATH_SUFFIXES imgui)
    find_path(IMGUI_BACKENDS_INCLUDE_DIR NAMES imgui_impl_glfw.h PATH_SUFFIXES imgui/backends)

    # Add imgui as SYSTEM to suppress warnings from third-party headers.
    # Both the main imgui dir and backends dir are needed because the backend
    # .cpp files use quoted includes like #include "imgui.h" and
    # #include "imgui_impl_glfw.h".
    target_include_directories(romulus_gui SYSTEM PRIVATE
        ${IMGUI_INCLUDE_DIR}
        ${IMGUI_BACKENDS_INCLUDE_DIR}
    )
endif()

# ── Link libraries ─────────────────────────────────────────────
if(imgui_FOUND)
    set(IMGUI_LIB imgui::imgui)
else()
    set(IMGUI_LIB imgui)
endif()

target_link_libraries(romulus_gui
    PRIVATE
        romulus::lib
        ${IMGUI_LIB}
        glfw
        OpenGL::GL
)

# System imgui depends on libstb for font rendering
if(NOT imgui_FOUND)
    target_link_libraries(romulus_gui PRIVATE stb)
endif()

# ── Warnings ───────────────────────────────────────────────────
romulus_set_warnings(romulus_gui)

# Suppress warnings on third-party backend sources
if(IMGUI_BACKEND_SOURCES)
    set_source_files_properties(
        ${IMGUI_BACKEND_SOURCES}
        PROPERTIES
            COMPILE_OPTIONS "-w"
    )
endif()

# ── Copy DAT artifacts next to binary ─────────────────────────
add_custom_command(
    TARGET romulus_gui
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/dats"
            "$<TARGET_FILE_DIR:romulus_gui>/dats"
)

# Output as 'romulus-gui' executable
set_target_properties(romulus_gui PROPERTIES
    OUTPUT_NAME romulus-gui
)
