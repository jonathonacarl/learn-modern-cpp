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
        Vector()
        {
            m_arr = static_cast<T *>(::operator new(m_capacity * sizeof(T)));
        }

        explicit Vector(std::size_t capacity) : m_capacity{capacity}
        {
            m_arr = static_cast<T *>(::operator new(m_capacity * sizeof(T)));
        }

        ~Vector() { cleanup(m_arr, m_size); }

        Vector(const Vector &v) { deep_copy(v); }

        Vector &operator=(const Vector &v)
        {
            if (this == &v)
                return *this;
            cleanup(m_arr, m_size);
            deep_copy(v);
            return *this;
        }

        Vector(Vector &&v) noexcept { steal_and_reset(std::move(v)); }

        Vector &operator=(Vector &&v) noexcept
        {
            if (this == &v)
                return *this;
            cleanup(m_arr, m_size);
            steal_and_reset(std::move(v));
            return *this;
        }

        T &operator[](std::size_t i) { return m_arr[i]; }
        const T &operator[](std::size_t i) const { return m_arr[i]; }

        template <typename... Args>
        T &emplace_back(Args &&...args)
        {
            if (m_size == m_capacity)
                grow();

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
        void cleanup(T *arr, std::size_t size)
        {
            for (std::size_t i{}; i < size; ++i)
                arr[i].~T();
            ::operator delete(arr);
        }

        void steal_and_reset(Vector &&v)
        {
            m_capacity = v.m_capacity;
            m_size = v.m_size;
            m_arr = v.m_arr;

            v.m_capacity = 0;
            v.m_size = 0;
            v.m_arr = nullptr;
        }

        void deep_copy(const Vector &v)
        {
            m_arr = static_cast<T *>(::operator new(v.m_capacity * sizeof(T)));
            for (std::size_t i{}; i < v.m_size; ++i)
                new (m_arr + i) T(v.m_arr[i]);
            m_capacity = v.m_capacity;
            m_size = v.m_size;
        }

        void grow()
        {
            T *old_arr = m_arr;
            std::size_t old_size = m_size;

            m_capacity = m_capacity ? m_capacity * 2 : 1;
            m_size = 0;
            m_arr = static_cast<T *>(::operator new(m_capacity * sizeof(T)));
            for (std::size_t i{}; i < old_size; ++i)
                new (m_arr + i) T(std::move(old_arr[i])); // our move constructor is noexcept

            m_size = old_size;
            cleanup(old_arr, old_size);
        }

        std::size_t m_capacity{};
        std::size_t m_size{};
        T *m_arr{nullptr};
    };

} // namespace placement

#endif // VECTOR_RAW_H
