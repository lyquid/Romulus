# ── romulus_enable_clang_tidy ─────────────────────────────────
# Enable clang-tidy for all targets built after this call.
# Usage: romulus_enable_clang_tidy()
#
function(romulus_enable_clang_tidy)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy clang-tidy-17 clang-tidy-18)

    if(CLANG_TIDY_EXE)
        message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
        set(CMAKE_CXX_CLANG_TIDY
            "${CLANG_TIDY_EXE}"
            "--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy"
            "--header-filter=${CMAKE_SOURCE_DIR}/lib/.*"
            PARENT_SCOPE
        )
    else()
        message(WARNING "clang-tidy not found — static analysis disabled")
    endif()
endfunction()
