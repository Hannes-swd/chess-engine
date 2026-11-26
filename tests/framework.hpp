#pragma once

// Mini-Testframework ohne externe Abhaengigkeit.

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace testing {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

inline int& failures() {
    static int f = 0;
    return f;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

}  // namespace testing

#define TEST_CASE(name)                                         \
    static void name();                                         \
    static ::testing::Registrar registrar_##name(#name, name);  \
    static void name()

#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            ++::testing::failures();                                                           \
            std::cerr << "  " << __FILE__ << ":" << __LINE__ << ": CHECK fehlgeschlagen: " #cond \
                      << "\n";                                                                 \
        }                                                                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                                                  \
    do {                                                                                                \
        const auto va_ = (a);                                                                           \
        const auto vb_ = (b);                                                                           \
        if (!(va_ == vb_)) {                                                                            \
            ++::testing::failures();                                                                    \
            std::cerr << "  " << __FILE__ << ":" << __LINE__ << ": " #a " == " #b " fehlgeschlagen ("   \
                      << va_ << " != " << vb_ << ")\n";                                                 \
        }                                                                                               \
    } while (0)
