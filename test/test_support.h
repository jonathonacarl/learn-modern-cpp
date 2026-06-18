#ifndef TEST_SUPPORT_HPP
#define TEST_SUPPORT_HPP

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// Abort-on-failure assertion that reports the source line and the failing expression.
#define CHECK(cond)                                                   \
    do                                                                \
    {                                                                 \
        if (!(cond))                                                  \
        {                                                             \
            std::cout << "FAIL line " << __LINE__ << ": " #cond "\n"; \
            std::abort();                                             \
        }                                                             \
    } while (0)

// Instrumented element type. The static counters let tests assert there are no leaks,
// no double destroys, and that relocation during growth moves rather than copies.
struct Tracked
{
    static inline int alive = 0;
    static inline int moves = 0;
    int v;
    Tracked(int x = 0) : v(x) { ++alive; }
    Tracked(const Tracked &o) : v(o.v) { ++alive; }
    Tracked(Tracked &&o) noexcept : v(o.v)
    {
        o.v = -1;
        ++alive;
        ++moves;
    }
    Tracked &operator=(const Tracked &o)
    {
        v = o.v;
        return *this;
    }
    Tracked &operator=(Tracked &&o) noexcept
    {
        v = o.v;
        o.v = -1;
        return *this;
    }
    ~Tracked() { --alive; }
    bool operator==(const Tracked &o) const { return v == o.v; }
};

// Pull a comparable scalar out of an element so the oracle can compare value-by-value.
inline int value_of(int x) { return x; }
inline int value_of(const Tracked &t) { return t.v; }

// Element type of a vector-like container, deduced from operator[] so the container does
// not need to publish a value_type alias.
template <typename Vec>
using element_t = std::remove_cv_t<std::remove_reference_t<
    decltype(std::declval<Vec &>()[std::size_t{0}])>>;

// Differential / oracle test: drive the same pseudo-random op stream into MyVec and into
// a std::vector and assert they stay identical after every operation. T must be
// constructible from int (int and Tracked both are). Exercises both push_back overloads
// (lvalue copy, rvalue move), pop_back, and clear.
template <typename MyVec>
void differential_against_std(const char *label, unsigned seed = 0xC0FFEEu, int steps = 30000)
{
    using T = element_t<MyVec>;

    MyVec mine(1);
    std::vector<T> ref;

    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> op(0, 9);
    std::uniform_int_distribution<int> val(-1'000'000, 1'000'000);

    for (int step = 0; step < steps; ++step)
    {
        const int choice = op(rng);
        if (choice < 6) // ~60% push
        {
            const int x = val(rng);
            if (x & 1) // lvalue -> copy path
            {
                T tmp(x);
                mine.push_back(tmp);
                ref.push_back(tmp);
            }
            else // rvalue -> move path
            {
                mine.push_back(T(x));
                ref.push_back(T(x));
            }
        }
        else if (choice < 9) // ~30% pop
        {
            if (!ref.empty())
            {
                const int expected = value_of(ref.back());
                ref.pop_back();
                const int got = value_of(mine.pop_back());
                CHECK(got == expected);
            }
        }
        else // ~10% clear
        {
            mine.clear();
            ref.clear();
        }

        CHECK(mine.size() == ref.size());
        for (std::size_t i = 0; i < ref.size(); ++i)
            CHECK(value_of(mine[i]) == value_of(ref[i]));
    }

    std::cout << "  differential[" << label << "] ok (" << steps << " ops, final size "
              << mine.size() << ")\n";
}

// Same idea for a heap-owning element type (std::string), so the copy/move of a
// non-trivial T is checked against std::vector<std::string>.
template <typename MyVec>
void differential_strings(unsigned seed = 7u, int steps = 20000)
{
    MyVec mine(1);
    std::vector<std::string> ref;

    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> op(0, 9);
    std::uniform_int_distribution<int> len(0, 20);
    auto make = [](int n)
    { return std::string(static_cast<std::size_t>(n), char('a' + (n % 26))); };

    for (int step = 0; step < steps; ++step)
    {
        const int choice = op(rng);
        if (choice < 6)
        {
            const int n = len(rng);
            if (n & 1) // lvalue -> copy
            {
                std::string s = make(n);
                mine.push_back(s);
                ref.push_back(s);
            }
            else // rvalue -> move (rebuild an equal value for the oracle)
            {
                std::string s = make(n);
                mine.push_back(std::move(s));
                ref.push_back(make(n));
            }
        }
        else if (choice < 9)
        {
            if (!ref.empty())
            {
                std::string expected = ref.back();
                ref.pop_back();
                std::string got = mine.pop_back();
                CHECK(got == expected);
            }
        }
        else
        {
            mine.clear();
            ref.clear();
        }

        CHECK(mine.size() == ref.size());
        for (std::size_t i = 0; i < ref.size(); ++i)
            CHECK(mine[i] == ref[i]);
    }

    std::cout << "  differential[string] ok (" << steps << " ops, final size "
              << mine.size() << ")\n";
}

// Three-way differential: drive ONE op stream into two implementations (VecA, VecB) and a
// std::vector oracle at once, asserting all three agree after every op. This checks the
// two implementations against each other and against the stdlib simultaneously. T must be
// constructible from int.
template <typename VecA, typename VecB>
void cross_check(const char *label, unsigned seed = 0xC0FFEEu, int steps = 30000)
{
    using T = element_t<VecA>;
    static_assert(std::is_same_v<T, element_t<VecB>>,
                  "both implementations must hold the same element type");

    VecA a(1);
    VecB b(1);
    std::vector<T> ref;

    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> op(0, 9);
    std::uniform_int_distribution<int> val(-1'000'000, 1'000'000);

    for (int step = 0; step < steps; ++step)
    {
        const int choice = op(rng);
        if (choice < 6)
        {
            const int x = val(rng);
            if (x & 1) // lvalue -> copy on all three
            {
                T tmp(x);
                a.push_back(tmp);
                b.push_back(tmp);
                ref.push_back(tmp);
            }
            else // rvalue -> move on all three
            {
                a.push_back(T(x));
                b.push_back(T(x));
                ref.push_back(T(x));
            }
        }
        else if (choice < 9)
        {
            if (!ref.empty())
            {
                const int expected = value_of(ref.back());
                ref.pop_back();
                CHECK(value_of(a.pop_back()) == expected);
                CHECK(value_of(b.pop_back()) == expected);
            }
        }
        else
        {
            a.clear();
            b.clear();
            ref.clear();
        }

        CHECK(a.size() == ref.size());
        CHECK(b.size() == ref.size());
        for (std::size_t i = 0; i < ref.size(); ++i)
        {
            CHECK(value_of(a[i]) == value_of(ref[i]));
            CHECK(value_of(b[i]) == value_of(ref[i]));
        }
    }

    std::cout << "  cross_check[" << label << "] ok (" << steps << " ops, final size "
              << ref.size() << ")\n";
}

// Three-way differential for a heap-owning element type (std::string).
template <typename VecA, typename VecB>
void cross_check_strings(unsigned seed = 7u, int steps = 20000)
{
    VecA a(1);
    VecB b(1);
    std::vector<std::string> ref;

    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> op(0, 9);
    std::uniform_int_distribution<int> len(0, 20);
    auto make = [](int n)
    { return std::string(static_cast<std::size_t>(n), char('a' + (n % 26))); };

    for (int step = 0; step < steps; ++step)
    {
        const int choice = op(rng);
        if (choice < 6)
        {
            const int n = len(rng);
            if (n & 1) // copy
            {
                std::string s = make(n);
                a.push_back(s);
                b.push_back(s);
                ref.push_back(s);
            }
            else // move (rebuild an equal value per container)
            {
                a.push_back(make(n));
                b.push_back(make(n));
                ref.push_back(make(n));
            }
        }
        else if (choice < 9)
        {
            if (!ref.empty())
            {
                std::string expected = ref.back();
                ref.pop_back();
                CHECK(a.pop_back() == expected);
                CHECK(b.pop_back() == expected);
            }
        }
        else
        {
            a.clear();
            b.clear();
            ref.clear();
        }

        CHECK(a.size() == ref.size());
        CHECK(b.size() == ref.size());
        for (std::size_t i = 0; i < ref.size(); ++i)
        {
            CHECK(a[i] == ref[i]);
            CHECK(b[i] == ref[i]);
        }
    }

    std::cout << "  cross_check[string] ok (" << steps << " ops, final size "
              << ref.size() << ")\n";
}

#endif // TEST_SUPPORT_HPP