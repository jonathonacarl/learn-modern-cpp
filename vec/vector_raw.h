#ifndef VECTOR_RAW_H
#define VECTOR_RAW_H

#include <cstddef>
#include <new>
#include <utility>

namespace placement
{
    /*
     * A growable array that manages raw bytes with ::operator new and constructs elements in place with placement-new.
     */
    template <typename T>
    class Vector
    {
    public:
        // When capacity is unspecified, we don't need to allocate any raw memory at construction.
        Vector() = default;

        explicit Vector(std::size_t capacity) : m_capacity{capacity}
        {
            m_arr = static_cast<T *>(::operator new(m_capacity * sizeof(T)));
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

        Vector(Vector &&v) noexcept { _steal_and_reset(std::move(v)); }

        Vector &operator=(Vector &&v) noexcept
        {
            if (this == &v)
                return *this;
            _cleanup();
            _steal_and_reset(std::move(v));
            return *this;
        }

        T &operator[](std::size_t i) { return m_arr[i]; }
        const T &operator[](std::size_t i) const { return m_arr[i]; }

        template <typename... Args>
        T &emplace_back(Args &&...args)
        {
            if (m_size == m_capacity)
                _grow();

            T *p = new (m_arr + m_size) T(std::forward<Args>(args)...);
            ++m_size;
            return *p;
        }

        void push_back(const T &value) { emplace_back(value); }
        void push_back(T &&value) { emplace_back(std::move(value)); }

        std::size_t capacity() const { return m_capacity; }
        std::size_t size() const { return m_size; }

        T *begin() { return m_arr; }
        const T *begin() const { return m_arr; }
        T *end() { return m_arr + m_size; }
        const T *end() const { return m_arr + m_size; }

        T pop_back()
        {
            T value{};
            if (m_size == 0)
                return value;

            value = std::move(m_arr[m_size - 1]);
            m_arr[m_size - 1].~T();
            --m_size;
            return value;
        }

        void clear()
        {
            for (std::size_t i{}; i < m_size; ++i)
                m_arr[i].~T();
            m_size = 0;
        }

    private:
        void _cleanup()
        {
            for (std::size_t i{}; i < m_size; ++i)
            {
                m_arr[i].~T();
            }
            ::operator delete(m_arr);
        }

        void _steal_and_reset(Vector &&v)
        {
            m_capacity = v.m_capacity;
            m_size = v.m_size;
            m_arr = v.m_arr;

            v.m_capacity = 0;
            v.m_size = 0;
            v.m_arr = nullptr;
        }

        void _deep_copy(const Vector &v)
        {
            m_arr = static_cast<T *>(::operator new(v.m_capacity * sizeof(T)));
            for (std::size_t i{}; i < v.m_size; ++i)
                new (m_arr + i) T(v.m_arr[i]);
            m_capacity = v.m_capacity;
            m_size = v.m_size;
        }

        void _grow()
        {
            T *old_arr = m_arr;
            std::size_t old_size = m_size;

            std::size_t new_capacity = m_capacity ? m_capacity * 2 : 1;
            T *new_arr = static_cast<T *>(::operator new(new_capacity * sizeof(T)));

            std::size_t i{};
            try
            {
                for (; i < old_size; ++i)
                {
                    new (new_arr + i) T(std::move_if_noexcept(old_arr[i])); // our move constructor is noexcept
                }
            }
            catch (...)
            {
                for (std::size_t j{}; j < i; ++j)
                {
                    new_arr[j].~T();
                }
                ::operator delete(new_arr);
                throw;
            }

            // Allocation and construction succeeded.
            m_size = old_size;
            m_capacity = new_capacity;
            m_arr = new_arr;

            for (i = 0; i < old_size; ++i)
            {
                old_arr[i].~T();
            }
            ::operator delete(old_arr);
        }

        std::size_t m_capacity{};
        std::size_t m_size{};
        T *m_arr{nullptr};
    };

} // namespace placement

#endif // VECTOR_RAW_H
