#include "synthetic_fixture_lab.hpp"

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <span>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: romulus-fixture-generator <output-directory>\n";
    return 2;
  }

  // Keep the command-line wrapper deliberately thin: integration tests call the same generator
  // function directly, so manual fixtures and CI fixtures cannot acquire different semantics.
  const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));
  const auto result =
      romulus::test::generate_synthetic_fixture_lab(std::filesystem::path{arguments[1]});
  if (!result) {
    std::cerr << "Fixture generation failed: " << result.error().message << '\n';
    return 1;
  }

  std::cout << "Synthetic fixture lab generated at " << result->root << '\n'
            << "Manifest: " << result->manifest << '\n';
  return 0;
}
