#ifndef SHARED_PTR_H
#define SHARED_PTR_H

#include <atomic>

class ControlBlock
{
public:
    // The number of live shared_ptrs that own the object.
    std::atomic<int> m_strongRefCount{1};

    // Credit to https://stackoverflow.com/questions/75872721/how-do-shared-ptr-and-weak-ptr-avoid-a-leak-in-this-case catching a sneaky concurrency bug.
    // All outstanding `shared_ptr`s of a given control block will share ownership of one increment of the weak count. Call this the "strong group".
    // This approach means that a single atomic decides when the control block can be freed, so that the last shared_ptr and weak_ptr don't both try freeing it.
    std::atomic<int> m_weakRefCount{1};
};

template <typename T>
class weak_ptr;

template <typename T>
class shared_ptr
{
public:
    explicit shared_ptr(T *p) : m_data{p}, m_control{new ControlBlock} {}

    ~shared_ptr()
    {
        release();
    }

    shared_ptr(const shared_ptr &other) : m_data{other.m_data}, m_control{other.m_control} // copy ctor
    {
        // This object is now owned by someone else too, so share the control block and
        // bump the strong count. Increments can be relaxed: we already hold a reference
        // through other, so there is nothing to publish or acquire here.
        if (m_control)
            m_control->m_strongRefCount.fetch_add(1, std::memory_order_relaxed);
    }

    shared_ptr &operator=(const shared_ptr &other) // copy assignment
    {
        if (this == &other)
            return *this;

        release();

        m_data = other.m_data;
        m_control = other.m_control;

        if (m_control)
            m_control->m_strongRefCount.fetch_add(1, std::memory_order_relaxed);

        return *this;
    }

    shared_ptr(shared_ptr &&other) // move ctor
    {
        // Don't update the strong count since this is a steal.
        m_data = other.m_data;
        m_control = other.m_control;

        other.m_data = nullptr;
        other.m_control = nullptr;
    }

    shared_ptr &operator=(shared_ptr &&other) // move assignment
    {
        if (this == &other)
            return *this;

        release();

        // Don't update the strong count since this is a steal.
        m_control = other.m_control;
        m_data = other.m_data;

        other.m_data = nullptr;
        other.m_control = nullptr;
        return *this;
    }

    T &operator*() const
    {
        return *m_data;
    }

    T *operator->() const
    {
        return this->m_data;
    }

    bool expired() const
    {
        return m_control && m_control->m_strongRefCount.load(std::memory_order_relaxed) == 0;
    }

    int refCount() const
    {
        return m_control ? m_control->m_strongRefCount.load(std::memory_order_relaxed) : 0;
    }

private:
    friend class weak_ptr<T>;

    /*
     * Adopts an already counted strong reference. This is used only by weak_ptr::lock(),
     * which has already incremented the strong count via compare exchange, so this ctor
     * must not increment again.
     */
    shared_ptr(T *p, ControlBlock *b) : m_data(p), m_control(b) {}

    void release()
    {
        if (!m_control)
            return;

        // Drop our strong reference. fetch_sub returns the value from before the
        // subtraction, so a return of 1 means we were the last strong owner. Branching on
        // that return value, instead of re-reading the count, is what makes this safe under
        // threads: exactly one thread can observe the 1 and run the teardown.
        if (m_control->m_strongRefCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            delete m_data;
            if (m_control->m_weakRefCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
                delete m_control;
        }

        m_data = nullptr;
        m_control = nullptr;
    }

    T *m_data{nullptr};
    ControlBlock *m_control{nullptr};
};

template <typename T>
class weak_ptr
{
public:
    weak_ptr() = default; // ctor

    explicit weak_ptr(const shared_ptr<T> &sp) : m_data{sp.m_data}, m_control{sp.m_control}
    {
        if (m_control)
            m_control->m_weakRefCount.fetch_add(1, std::memory_order_relaxed);
    }

    ~weak_ptr() // dtor
    {
        release();
    }

    weak_ptr(const weak_ptr &other) : m_data(other.m_data), m_control(other.m_control) // copy ctor
    {
        if (m_control)
            m_control->m_weakRefCount.fetch_add(1, std::memory_order_relaxed);
    }

    weak_ptr &operator=(const weak_ptr &other) // copy assignment
    {
        if (this == &other)
            return *this;

        release();

        m_data = other.m_data;
        m_control = other.m_control;

        if (m_control)
            m_control->m_weakRefCount.fetch_add(1, std::memory_order_relaxed);

        return *this;
    }

    weak_ptr(weak_ptr &&other) // move ctor
    {
        m_data = other.m_data;
        m_control = other.m_control;

        // Stolen.
        other.m_data = nullptr;
        other.m_control = nullptr;
    }

    weak_ptr &operator=(weak_ptr &&other) // move assignment
    {
        if (this == &other)
            return *this;

        // Release the weak reference we currently hold before taking the new one,
        // otherwise that reference leaks.
        release();

        m_data = other.m_data;
        m_control = other.m_control;

        // Stolen.
        other.m_data = nullptr;
        other.m_control = nullptr;

        return *this;
    }

    shared_ptr<T> lock() const
    {
        if (!m_control)
            return shared_ptr<T>(nullptr, nullptr);

        // Try to take a strong reference, but only if one still exists. If the strong count
        // is already zero, the object is gone and we must not revive it.
        int cur = m_control->m_strongRefCount.load(std::memory_order_relaxed);
        while (cur != 0)
        {
            if (m_control->m_strongRefCount.compare_exchange_weak(
                    cur, cur + 1, std::memory_order_acq_rel, std::memory_order_relaxed))
                return shared_ptr<T>(m_data, m_control); // adopts the reference we just took

            // cur was refreshed with the live value on failure, so loop and retry.
        }

        return shared_ptr<T>(nullptr, nullptr);
    }

    bool expired() const
    {
        return m_control && m_control->m_strongRefCount.load(std::memory_order_relaxed) == 0;
    }

private:
    void release()
    {
        if (!m_control)
            return;

        // A weak_ptr only ever holds a weak reference. The last owner to leave, counting the
        // strong group as one, frees the control block.
        if (m_control->m_weakRefCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
            delete m_control;

        m_data = nullptr;
        m_control = nullptr;
    }

    T *m_data{nullptr};
    ControlBlock *m_control{nullptr};
};

#endif