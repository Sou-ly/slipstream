#include <print>

// Fails the build if the standard ever silently regresses -- a wrong -std flag
// becomes a compile error here instead of a confusing one at the first C++23
// feature the code happens to use.
static_assert(__cplusplus >= 202302L, "slipstream requires C++23");

int main() {
    std::println("slipstream");
    return 0;
}
