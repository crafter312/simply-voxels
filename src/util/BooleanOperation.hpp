#ifndef BOOLEAN_OPERATION_HPP
#define BOOLEAN_OPERATION_HPP

namespace BooleanBiFunction {

    // Each function takes two boolean inputs (a, b) and returns a boolean result.

    constexpr bool FALSE(bool a, bool b) { (void)a; (void)b; return false; }
    constexpr bool NOT_OR(bool a, bool b) { return !a && !b; } // !(a || b)
    constexpr bool ONLY_SECOND(bool a, bool b) { return b && !a; }
    constexpr bool NOT_FIRST(bool a, bool b) { (void)b; return !a; }
    constexpr bool ONLY_FIRST(bool a, bool b) { return a && !b; }
    constexpr bool NOT_SECOND(bool a, bool b) { (void)a; return !b; }
    constexpr bool NOT_SAME(bool a, bool b) { return a != b; } // a XOR b
    constexpr bool NOT_AND(bool a, bool b) { return !a || !b; } // !(a && b)
    constexpr bool AND(bool a, bool b) { return a && b; }
    constexpr bool SAME(bool a, bool b) { return a == b; } // !(a XOR b)
    constexpr bool SECOND(bool a, bool b) { (void)a; return b; }
    constexpr bool CAUSES(bool a, bool b) { return !a || b; } // a implies b
    constexpr bool FIRST(bool a, bool b) { (void)b; return a; }
    constexpr bool CAUSED_BY(bool a, bool b) { return a || !b; } // b implies a
    constexpr bool OR(bool a, bool b) { return a || b; }
    constexpr bool TRUE(bool a, bool b) { (void)a; (void)b; return true; }

    // You could also define these as std::function objects if you needed to pass them around
    // as first-class citizens more dynamically, but constexpr functions are generally preferred
    // for this kind of static, known-at-compile-time logic.
    // Example using std::function (would require #include <functional>):
    // static const std::function<bool(bool, bool)> F_OR = [](bool a, bool b) { return a || b; };

} // namespace BooleanBiFunction

#endif // BOOLEAN_OPERATION_HPP