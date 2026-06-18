// Combined test suite for both Vector implementations:
//   alloc::Vector      (vector_allocator.h, std::allocator_traits based)
//   placement::Vector  (vector_raw.h, ::operator new + placement-new)
//
// Layers:
//   1. Three-way cross-check (cross_check): one random op stream drives both
//      implementations AND a std::vector oracle, comparing all three after every op.
//      This validates the two implementations against each other and against the stdlib.
//   2. Shared invariant suite (run_invariants): the lifetime / move-vs-copy checks that a
//      value oracle cannot express, run against each implementation in turn.

#include "vector_allocator.h"
#include "vector_raw.h"
#include "test_support.h"

#include <iostream>
#include <string>

// The hand-written invariant suite, parameterized on the Vector template so it can run
// against either implementation. (Variadic template-template param matches both the
// 2-parameter alloc::Vector and the 1-parameter placement::Vector.)
template <template <typename...> class Vec>
void run_invariants(const char *impl)
{
    std::cout << "invariants [" << impl << "]:\n";

    // ---- core: push, access, growth ----
    {
        Vec<int> v(2);
        v.push_back(10);
        v.push_back(20);
        CHECK(v.size() == 2 && v[0] == 10 && v[1] == 20);
        std::cout << "  1 push/access ok\n";
    }
    {
        Vec<int> v(2);
        for (int i = 0; i < 9; ++i)
            v.push_back(i);
        CHECK(v.size() == 9 && v.capacity() >= 9);
        for (int i = 0; i < 9; ++i)
            CHECK(v[i] == i); // survived realloc
        std::cout << "  2 growth ok\n";
    }

    // ---- copy is deep & independent ----
    {
        Vec<int> a(2);
        a.push_back(1);
        a.push_back(2);
        Vec<int> b = a; // copy ctor
        b[0] = 99;
        CHECK(a[0] == 1 && b[0] == 99);
        Vec<int> c(2);
        c.push_back(7);
        c = a; // copy assignment (overwrite existing)
        CHECK(c.size() == 2 && c[0] == 1);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign-overloaded"
#endif
        c = c; // self copy-assign, must not corrupt
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
        CHECK(c.size() == 2 && c[0] == 1);
        std::cout << "  3 copy ok\n";
    }

    // ---- move empties source, self-move safe ----
    {
        Vec<int> a(2);
        a.push_back(7);
        Vec<int> b = std::move(a); // move ctor
        CHECK(b[0] == 7 && a.size() == 0);
        Vec<int> c(2);
        c.push_back(1);
        c.push_back(2);
        Vec<int> d(2);
        d.push_back(9);
        d = std::move(c); // move assignment
        CHECK(d.size() == 2 && d[0] == 1 && c.size() == 0);
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wself-move"
#endif
        d = std::move(d); // self move-assign, must survive (deliberate)
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        CHECK(d.size() == 2 && d[0] == 1);
        std::cout << "  4 move ok\n";
    }

    // ---- lifetime accounting: no leaks, no double-free ----
    {
        CHECK(Tracked::alive == 0);
        {
            Vec<Tracked> v(1);
            CHECK(Tracked::alive == 0); // capacity alloc constructs nothing
            for (int i = 0; i < 20; ++i)
                v.emplace_back(i);
            CHECK(Tracked::alive == 20);
            Vec<Tracked> c = v; // copy
            CHECK(Tracked::alive == 40);
            Vec<Tracked> m = std::move(v); // move (no new objects)
            CHECK(Tracked::alive == 40);
        }
        CHECK(Tracked::alive == 0); // everything cleaned up
        std::cout << "  5 lifetime ok\n";
    }

    // ---- growth MOVES rather than copies ----
    {
        Tracked::moves = 0;
        Vec<Tracked> v(1);
        for (int i = 0; i < 8; ++i)
            v.emplace_back(i);
        for (int i = 0; i < 8; ++i)
            CHECK(v[i].v == i);
        CHECK(Tracked::moves > 0);
        std::cout << "  6 growth-moves ok (moves=" << Tracked::moves << ")\n";
    }

    // ---- empty / zero-capacity edges (the &v[0]-style crash guard) ----
    {
        Vec<Tracked> z(0);
        z.emplace_back(1); // grow from 0
        CHECK(z.size() == 1 && z[0].v == 1);
        Vec<Tracked> e(3);
        Vec<Tracked> moved = std::move(e); // move an EMPTY vector
        CHECK(moved.size() == 0);
        std::cout << "  7 empty-edges ok\n";
    }

    // ---- begin/end + range-based for ----
    {
        Vec<int> v(2);
        v.push_back(1);
        v.push_back(2);
        v.push_back(3);
        int sum = 0;
        for (int x : v)
            sum += x; // non-const iteration
        CHECK(sum == 6);
        for (int &x : v)
            x *= 10; // modify through iterator
        CHECK(v[0] == 10 && v[2] == 30);
        CHECK(v.end() - v.begin() == 3); // pointer arithmetic == size
        std::cout << "  8 begin/end ok\n";
    }
    {
        Vec<int> v(2);
        v.push_back(5);
        v.push_back(6);
        const Vec<int> &cv = v; // forces const overloads
        int sum = 0;
        for (int x : cv)
            sum += x;
        CHECK(sum == 11);
        std::cout << "  9 const-iter ok\n";
    }

    // ---- pop_back: returns value, shrinks, frees object ----
    {
        CHECK(Tracked::alive == 0);
        Vec<Tracked> v(4);
        v.emplace_back(1);
        v.emplace_back(2);
        v.emplace_back(3);
        CHECK(Tracked::alive == 3);
        {
            Tracked popped = v.pop_back(); // by value
            CHECK(popped.v == 3);
            CHECK(v.size() == 2);       // shrank
            CHECK(Tracked::alive == 3); // 2 in vector + popped local
        }
        CHECK(Tracked::alive == 2); // popped local destroyed
        std::cout << "  10 pop_back ok\n";
    }
    {
        Vec<int> v(2);
        v.push_back(1);
        v.push_back(2);
        v.pop_back();
        v.pop_back();
        CHECK(v.size() == 0);
        v.push_back(99); // reuse after emptying
        CHECK(v.size() == 1 && v[0] == 99);
        std::cout << "  11 pop-then-reuse ok\n";
    }

    // ---- clear: destroys all, keeps capacity, reusable ----
    {
        CHECK(Tracked::alive == 0);
        Vec<Tracked> v(8);
        for (int i = 0; i < 5; ++i)
            v.emplace_back(i);
        CHECK(Tracked::alive == 5);
        size_t cap = v.capacity();
        v.clear();
        CHECK(v.size() == 0 && Tracked::alive == 0 && v.capacity() == cap);
        v.emplace_back(42);
        CHECK(v.size() == 1 && v[0].v == 42);
        std::cout << "  12 clear ok\n";
    }

    // ---- non-trivial heap-owning T (std::string) + emplace in place ----
    {
        Vec<std::string> v(1);
        v.push_back("hello");
        v.emplace_back(5, 'x'); // constructs std::string(5,'x') = "xxxxx"
        std::string s = "world";
        v.push_back(s);            // lvalue -> copy
        v.push_back(std::move(s)); // rvalue -> move
        CHECK(v.size() == 4);
        CHECK(v[0] == "hello" && v[1] == "xxxxx" && v[2] == "world");
        std::cout << "  13 string/emplace ok\n";
    }
}

int main()
{
    // ===== three-way conformance: alloc::Vector vs placement::Vector vs std::vector =====
    std::cout << "cross-check (alloc vs placement vs std::vector):\n";
    cross_check<alloc::Vector<int>, placement::Vector<int>>("int");

    CHECK(Tracked::alive == 0);
    cross_check<alloc::Vector<Tracked>, placement::Vector<Tracked>>("Tracked");
    CHECK(Tracked::alive == 0); // oracle churn left nothing alive

    cross_check_strings<alloc::Vector<std::string>, placement::Vector<std::string>>();
    std::cout << "\n";

    // ===== shared invariant suite, run against each implementation =====
    run_invariants<alloc::Vector>("alloc");
    std::cout << "\n";
    run_invariants<placement::Vector>("placement");

    std::cout << "\nALL TESTS PASSED\n";
}