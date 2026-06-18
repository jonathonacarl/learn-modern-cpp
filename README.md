# data-structures

A learning repository where I reimplement a few fundamental data structures from the C++
standard library, by hand, in modern C++17. The goal is not to compete with the stdlib but
to understand what happens under the hood: how ownership, allocation, element lifetime, and
memory ordering actually work once you strip away the library abstractions.

Each implementation is validated against its standard-library counterpart (for example,
`Vector` against `std::vector`, `shared_ptr` against `std::shared_ptr`), so "does mine
behave like the real thing" is an executable question rather than a guess.

## Layout

| Directory     | Classes                                                | What it is |
|---------------|--------------------------------------------------------|------------|
| `vec/`        | `alloc::Vector<T, Allocator>`, `placement::Vector<T>`  | Two takes on a growable contiguous array. `vector_allocator.h` manages storage through `std::allocator_traits`; `vector_raw.h` uses raw `::operator new` plus placement-new and explicit destructor calls. |
| `spsc/`       | `SPSC_RingBuffer<T, Capacity>`                         | A lock-free single-producer / single-consumer queue built on atomic head/tail indices with acquire/release publishing and cache-line separation. |
| `shared_ptr/` | `shared_ptr<T>`, `weak_ptr<T>`, `ControlBlock`         | Reference-counted smart pointers backed by a control block with atomic strong and weak counts. |
| `test/`       | test harness                                           | Build-and-run test suites plus shared utilities. See `test/README.md`. |

Per-directory READMEs (in `vec/`, `spsc/`, `shared_ptr/`) cover what I learned in each
implementation. This top-level file is just the map.

## Building and running

Requires a C++17 compiler and `make`. Test binaries are written to `bin/`.

```
make test    # build and run every suite
make tsan    # ThreadSanitizer builds of the concurrent types (ring buffer, shared_ptr)
make clean
```

As configured in the `Makefile`, the manually memory-managed types (the vectors and
`shared_ptr`) build under AddressSanitizer and UndefinedBehaviorSanitizer, and the
concurrent types additionally have ThreadSanitizer builds via `make tsan` to check the
atomic ordering.

## A note on the tests

Everything under `test/` was written by Claude (Anthropic's assistant): the assertions, the
differential oracles that compare each type against the standard library, and the README in
that directory. I have not authored or line-by-line audited that harness and do not claim it
as my own work. The data-structure implementations under `vec/`, `spsc/`, and `shared_ptr/`
are mine.
