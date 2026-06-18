// Test suite for the lock-free SPSC ring buffer (spsc_ring_buffer.h).
//
// There is no concurrent stdlib container to diff against, so the "stdlib oracle" here is
// a std::queue used as a sequential reference model: a single-threaded random op stream
// is applied to both, and the ring buffer's success/failure and FIFO order are checked
// against the model (which also enforces the capacity bound explicitly). The original
// single-threaded, wraparound, and one-producer/one-consumer tests are retained.
//
// Build the thread-sanitized variant (make spsc_ring_buffer_tsan) to validate the
// acquire/release ordering on the atomics.

#include "../spsc/spsc_ring_buffer.h"
#include "../test/test_support.h" // CHECK

#include <iostream>
#include <queue>
#include <random>
#include <thread>

#ifndef SPSC_N
#define SPSC_N 1000000
#endif

int main()
{
    // --- single-threaded correctness ---
    {
        SPSC_RingBuffer<int, 4> rb;
        int out;
        for (int i = 0; i < 4; ++i) // capacity 4 holds all 4
            CHECK(rb.push(i));
        CHECK(!rb.push(99)); // full
        for (int i = 0; i < 4; ++i)
        {
            CHECK(rb.pop(out));
            CHECK(out == i); // FIFO order
        }
        CHECK(!rb.pop(out)); // empty
        std::cout << "single-threaded ok\n";
    }

    // --- wraparound: cycle indices past Capacity many times ---
    {
        SPSC_RingBuffer<int, 4> rb;
        int out;
        for (int round = 0; round < 1000; ++round)
        {
            CHECK(rb.push(round));
            CHECK(rb.pop(out) && out == round);
        }
        std::cout << "wraparound ok\n";
    }

    // --- oracle: random ops vs a std::queue reference model (bound + FIFO) ---
    {
        constexpr std::size_t Cap = 8;
        SPSC_RingBuffer<int, Cap> rb;
        std::queue<int> model;

        std::mt19937 rng(2024);
        std::uniform_int_distribution<int> coin(0, 1);
        int next = 0;

        for (int step = 0; step < 200000; ++step)
        {
            if (coin(rng) == 0) // attempt push
            {
                const bool expected = model.size() < Cap;
                const bool ok = rb.push(next);
                CHECK(ok == expected);
                if (ok)
                {
                    model.push(next);
                    ++next;
                }
            }
            else // attempt pop
            {
                int out = -1;
                const bool expected = !model.empty();
                const bool ok = rb.pop(out);
                CHECK(ok == expected);
                if (ok)
                {
                    CHECK(out == model.front());
                    model.pop();
                }
            }
        }
        std::cout << "oracle (std::queue model) ok\n";
    }

    // --- concurrent: one producer, one consumer, verify in-order, no gaps ---
    {
        constexpr int N = SPSC_N;
        SPSC_RingBuffer<int, 1024> rb;

        std::thread producer([&]
                             {
            for (int i = 0; i < N; ++i)
                while (!rb.push(i)) { /* spin until space */ } });

        std::thread consumer([&]
                             {
            int expected = 0, out;
            while (expected < N) {
                if (rb.pop(out)) {
                    CHECK(out == expected);
                    ++expected;
                }
            } });

        producer.join();
        consumer.join();
        std::cout << "concurrent ok (" << N << " items, in order)\n";
    }

    std::cout << "\nALL TESTS PASSED\n";
    return 0;
}