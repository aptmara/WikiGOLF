#include "src/core/StringUtils.h"
#include <iostream>
#define CHECK(condition, message)                                              \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "[FAIL] " << message << "\n";                               \
      return 1;                                                                \
    } else {                                                                   \
      std::cout << "[PASS] " << message << "\n";                               \
    }                                                                          \
  } while (0)

int main() {
  std::cout << "Running StringUtils trim tests...\n";

  const std::string text = "abcあいう漢字";

  auto trimmed = core::TrimUtf8ToLength(text, 4);
  CHECK(core::ToWString(trimmed).size() == 4, "Trimmed to 4 code points");
  CHECK(core::ToWString(trimmed) == L"abcあ",
        "Trim keeps original ordering and encoding");

  auto unlimited = core::TrimUtf8ToLength(text, 0);
  CHECK(unlimited == text, "Zero limit does not trim");

  auto largerLimit = core::TrimUtf8ToLength(text, 100);
  CHECK(largerLimit == text, "Larger limit leaves string untouched");

  std::cout << "All StringUtils trim tests passed.\n";
  return 0;
}
