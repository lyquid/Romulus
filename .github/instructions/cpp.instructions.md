---
applyTo: "**/*.{hpp,cpp,h}"
---

# C++ Source File Instructions

When generating or modifying C++ files in this project:

1. **Standard**: Use C++23 features (`std::expected`, `std::ranges`, `constexpr`, structured bindings)
2. **Error handling**: Return `Result<T>` (alias for `std::expected<T, romulus::Error>`), never throw exceptions
3. **RAII**: All resources must be managed via RAII — no manual `new`/`delete`
4. **Namespace**: All code under `namespace romulus` with sub-namespaces matching directory structure
5. **Headers**: Use `#pragma once`, `.hpp` extension, self-contained
6. **Naming**: Types=`PascalCase`, functions/vars=`snake_case`, members=`trailing_underscore_`, constants=`k_PascalCase`
7. **Formatting**: K&R braces (opening brace on same line), 2-space indent, 100 char line limit, always use braces
8. **Functions**: `[[nodiscard]]` on non-void returns, `const&` for input, return values over out-params
9. **Include order**: Own header → project headers → third-party → STL (blank lines between groups)
10. **Constructors**: Mark single-argument constructors `explicit`
11. **Documentation**: `///` Doxygen comments on public API — explain **why**, not what
