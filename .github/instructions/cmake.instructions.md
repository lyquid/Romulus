---
applyTo: "**/CMakeLists.txt"
---

# CMake File Instructions

When generating or modifying CMakeLists.txt files:

1. **Minimum version**: `cmake_minimum_required(VERSION 3.25)`
2. **C++ standard**: Set via `target_compile_features(target PUBLIC cxx_std_23)`, not global variables
3. **Target-scoped**: Always use `target_include_directories`, `target_link_libraries`, `target_compile_options` — never global equivalents
4. **No global pollution**: No `include_directories()`, `link_libraries()`, `add_definitions()`
5. **Dependencies**: All from vcpkg via `find_package()` — no `FetchContent`, no vendored code
6. **Naming**: Library targets as `romulus_<module>`, test targets as `romulus_test_<module>`
7. **Testing**: Guard with `if(BUILD_TESTING)` and `include(CTest)`
8. **Warnings**: Apply via `romulus_set_warnings(target)` from `cmake/CompilerWarnings.cmake`
