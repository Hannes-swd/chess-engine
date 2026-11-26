#include "framework.hpp"

#include <chrono>
#include <exception>

int main() {
    int failedCases = 0;
    for (const auto& c : testing::registry()) {
        const int before = testing::failures();
        const auto t0 = std::chrono::steady_clock::now();
        try {
            c.fn();
        } catch (const std::exception& e) {
            ++testing::failures();
            std::cerr << "  Ausnahme: " << e.what() << "\n";
        }
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        const bool ok = testing::failures() == before;
        if (!ok) ++failedCases;
        std::cout << (ok ? "[ OK ] " : "[FAIL] ") << c.name << " (" << ms << " ms)\n";
    }
    std::cout << "\n" << testing::registry().size() - static_cast<size_t>(failedCases) << "/"
              << testing::registry().size() << " Tests bestanden\n";
    return failedCases == 0 ? 0 : 1;
}
