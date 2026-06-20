#ifndef VECTOR_ALLOCATOR_H
#define VECTOR_ALLOCATOR_H

#include <cstddef>
#include <memory>
#include <utility>

namespace alloc
{

    /*
     * A growable array that routes all storage through std::allocator_traits, keeping raw
     * allocation separate from element construction/destruction.
     *
     * This class differs from vector-raw in that an Allocator template parameter is permitted.
     * This allows users to provide their own allocator. For simplicity, we've chosen std::allocator.
     */
    template <typename T, typename Allocator = std::allocator<T>>
    class Vector
    {
    public:
        Vector() = default;
        explicit Vector(std::size_t capacity) : m_capacity{capacity}
        {
            m_data = AT::allocate(m_alloc, m_capacity);
        }

        Vector(std::size_t size, const T &value) : m_size{size}, m_capacity{2 * size}
        {
            m_data = AT::allocate(m_alloc, m_capacity);
            for (std::size_t i{}; i < size; ++i)
                AT::construct(m_alloc, &m_data[i], value);
        }

        ~Vector() { _cleanup(); }

        Vector(const Vector &v) { _deep_copy(v); }

        Vector &operator=(const Vector &v)
        {
            if (this == &v)
                return *this;
            _cleanup();
            _deep_copy(v);
            return *this;
        }

        Vector(Vector &&v) noexcept { _steal_and_reset(v); }

        Vector &operator=(Vector &&v) noexcept
        {
            if (this == &v)
                return *this;
            _cleanup();
            _steal_and_reset(v);
            return *this;
        }

        T &operator[](std::size_t i) { return m_data[i]; }
        const T &operator[](std::size_t i) const { return m_data[i]; }

        template <typename... Args>
        T &emplace_back(Args &&...args)
        {
            if (m_size == m_capacity)
                _grow();

            T *p = &m_data[m_size];
            AT::construct(m_alloc, p, std::forward<Args>(args)...);
            ++m_size;
            return *p;
        }

        void push_back(const T &value) { emplace_back(value); }
        void push_back(T &&value) { emplace_back(std::move(value)); }

        T pop_back()
        {
            T value{};
            if (m_size == 0)
                return value;

            value = std::move(m_data[m_size - 1]);
            AT::destroy(m_alloc, &m_data[m_size - 1]);
            --m_size;
            return value;
        }

        void clear()
        {
            for (std::size_t i{}; i < m_size; ++i)
                AT::destroy(m_alloc, &m_data[i]);
            m_size = 0;
        }

        std::size_t size() const { return m_size; }
        std::size_t capacity() const { return m_capacity; }

        T *begin() { return m_data; }
        const T *begin() const { return m_data; }
        T *end() { return m_data + m_size; }
        const T *end() const { return m_data + m_size; }

    private:
        // Provides fine-grained control on allocation, construction, destruction, and deallocation of objects.
        // This is the "safer" version of calling ::operator new, ::operator delete, T(), and ~T() directly at
        // different places in this class.
        using AT = std::allocator_traits<Allocator>;

        std::size_t m_size{};
        std::size_t m_capacity{};
        T *m_data{nullptr};
        Allocator m_alloc{};

        void _cleanup()
        {
            for (std::size_t i{}; i < m_size; ++i)
            {
                AT::destroy(m_alloc, &m_data[i]);
            }
            AT::deallocate(m_alloc, m_data, m_capacity);
        }

        void _deep_copy(const Vector &v)
        {
            m_size = v.m_size;
            m_capacity = v.m_capacity;
            m_data = AT::allocate(m_alloc, m_capacity);
            for (std::size_t i{}; i < m_size; ++i)
                AT::construct(m_alloc, &m_data[i], v.m_data[i]);
        }

        void _steal_and_reset(Vector &v)
        {
            m_capacity = v.m_capacity;
            m_size = v.m_size;
            m_data = v.m_data;

            v.m_capacity = 0;
            v.m_size = 0;
            v.m_data = nullptr;
        }

        void _grow()
        {
            T *temp = m_data;
            std::size_t old_capacity = m_capacity;
            std::size_t old_size = m_size;

            std::size_t new_capacity = m_capacity ? old_capacity * 2 : 1;
            // Invokes ::operator new (malloc) under the hood, so there is no notion of realloc.
            // A custom allocator would be required here to attempt a realloc, if it was possible in the allocator's existing arena.
            T *new_data = AT::allocate(m_alloc, new_capacity);
            std::size_t i{};
            try
            {
                for (; i < old_size; ++i)
                {
                    // Call the copy ctor if move is not noexcept; the copy ctor can still throw, however.
                    AT::construct(m_alloc, &new_data[i], std::move_if_noexcept(temp[i]));
                }
            }
            catch (...)
            {
                for (std::size_t j{}; j < i; ++j)
                {
                    AT::destroy(m_alloc, &new_data[j]);
                }
                AT::deallocate(m_alloc, new_data, new_capacity);
                throw;
            }

            // Allocation and construction succeeded.
            m_size = old_size;
            m_capacity = new_capacity;
            m_data = new_data;

            // C++ Core Guidelines C.64: Destroying a moved-from object is safe: a move leaves the source in a valid, destructible state.
            for (i = 0; i < old_size; ++i)
            {
                AT::destroy(m_alloc, &temp[i]);
            }

            AT::deallocate(m_alloc, temp, old_capacity);
        }
    };

} // namespace alloc

#endif // VECTOR_ALLOCATOR_H
