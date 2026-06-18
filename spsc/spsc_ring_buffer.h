#ifndef SPSC_RING_BUFFER_H
#define SPSC_RING_BUFFER_H

#include <array>
#include <atomic>
#include <cstddef>

/*
 * A Single Producer, Single Consumer Ring Buffer implementation built on top of std::array.
 * This class exercises understanding of Sequential Consistency for Data Race Free (SC-DRF) programs with std::memory_order.
 */
template <typename T, std::size_t Capacity>
class SPSC_RingBuffer
{
public:
    bool push(const T &value) // called by producer.
    {
        const std::size_t head = m_head.load(std::memory_order_relaxed); // Producer calls this, so it's the only one looking.
        const std::size_t tail = m_tail.load(std::memory_order_acquire); // Producer needs to acquire what the consumer has published.
        bool full = head - tail == Capacity;
        if (!full)
        {
            m_buffer[head % Capacity] = value;
            m_head.store(head + 1, std::memory_order_release); // Producer broadcasts the buffer update to the consumer.
        }

        return !full;
    }

    bool pop(T &out) // called by consumer.
    {
        const std::size_t head = m_head.load(std::memory_order_acquire); // Consumer needs to acquire what the producer has published.
        const std::size_t tail = m_tail.load(std::memory_order_relaxed); // Consumer calls this, so it's the only one looking.
        bool is_empty = head == tail;
        if (!is_empty)
        {
            out = m_buffer[tail % Capacity];
            m_tail.store(tail + 1, std::memory_order_release); // Consumer broadcasts the buffer update to the producer.
        }
        return !is_empty;
    }

    // Rule of 0. We're using well-defined STL members, so we need no ctors, dtors, or assignments required.
private:
    std::array<T, Capacity> m_buffer;
    // Keep the two indices on separate cache lines: the producer hammers m_head,
    // the consumer hammers m_tail, and the cross-reads are the only sharing. Without
    // this they'd ping-pong one line via the MESI RFO/invalidate path (false sharing).
    alignas(64) std::atomic<std::size_t> m_head{0};
    alignas(64) std::atomic<std::size_t> m_tail{0};
};

#endif
