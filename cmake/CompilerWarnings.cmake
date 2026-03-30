# ── romulus_set_warnings ──────────────────────────────────────
# Apply strict compiler warnings to a target.
# Usage: romulus_set_warnings(my_target)
#
function(romulus_set_warnings target)
    set(MSVC_WARNINGS
        /W4         # High warning level
        /WX         # Warnings as errors
        /permissive- # Strict standards conformance
        /w14242     # Conversion warnings
        /w14254     # Operator conversion warnings
        /w14263     # Member function hiding
        /w14265     # Class has virtual functions but destructor is not virtual
        /w14287     # Unsigned/negative constant mismatch
        /w14296     # Expression is always true/false
        /w14311     # Pointer truncation
        /w14545     # Expression before comma evaluates to function
        /w14546     # Function call before comma missing argument list
        /w14547     # Operator before comma has no effect
        /w14549     # Operator before comma has no effect
        /w14555     # Expression has no effect
        /w14619     # Unknown pragma warning
        /w14640     # Thread-unsafe static member initialization
        /w14826     # Conversion is sign-extended
        /w14905     # Wide string literal cast to LPSTR
        /w14906     # String literal cast to LPWSTR
        /w14928     # Illegal copy-initialization
    )

    set(CLANG_WARNINGS
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wconversion
        -Wshadow
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
        -Wmisleading-indentation
        -Wunused
        -Woverloaded-virtual
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
    )

    set(GCC_WARNINGS
        ${CLANG_WARNINGS}
        -Wduplicated-cond
        -Wduplicated-branches
        -Wlogical-op
        -Wuseless-cast
    )

    if(MSVC)
        set(PROJECT_WARNINGS ${MSVC_WARNINGS})
    elseif(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
        set(PROJECT_WARNINGS ${CLANG_WARNINGS})
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(PROJECT_WARNINGS ${GCC_WARNINGS})
    else()
        message(AUTHOR_WARNING "Unknown compiler '${CMAKE_CXX_COMPILER_ID}' — no warnings set")
        return()
    endif()

    target_compile_options(${target} PRIVATE ${PROJECT_WARNINGS})
endfunction()
