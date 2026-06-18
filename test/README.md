# test/

All code in this directory was written by Claude (Anthropic's assistant), including this
file. It exists to check the implementations in `vec/`, `spsc/`, and `shared_ptr/` against
the standard library.

The general approach is differential (oracle) testing: the same sequence of operations is
applied to a hand-rolled type and to its stdlib equivalent, and the two are compared after
every step. On top of that, each suite adds hand-written invariant checks for properties a
value comparison cannot see, such as "no leaks" and "growth moves instead of copies." Most
suites are built under sanitizers (see the top-level README), which is where use-after-free,
leaks, and data races would surface.

A failed check prints the file, line, and expression, then aborts, so the first failure is
easy to locate.

## Files

### `test_support.h`
Shared utilities included by the other suites. Not a test on its own. It provides:
- `CHECK(cond)` — assert-or-abort with location reporting.
- `Tracked` — an instrumented element type that counts live instances and move operations,
  used to detect leaks, double frees, and copy-vs-move behavior.
- `element_t<Vec>` — deduces a container's element type from `operator[]`.
- the oracle helpers (`cross_check`, `cross_check_strings`, and the single-container
  `differential_*` variants) that drive a shared random op stream into the implementations
  and a stdlib container and compare them.

### `test_vector.cpp`
Exercises both vector implementations (`alloc::Vector` and `placement::Vector`). Oracle:
`std::vector`. Two layers:
- A three-way cross-check: one random op stream (push by copy and by move, pop, clear) is
  applied to `alloc::Vector`, `placement::Vector`, and `std::vector` at once, comparing all
  three after every operation. Run for `int`, the instrumented `Tracked`, and `std::string`.
- An invariant suite run against each implementation, covering: basic push / index / growth;
  survival of elements across reallocation; deep and independent copies (including
  self copy-assign); move emptying the source and self-move safety; lifetime accounting with
  no leaks or double frees; growth relocating by move rather than copy; empty and
  zero-capacity edge cases; `begin`/`end` and range-based iteration (const and non-const);
  `pop_back` returning a value and shrinking; reuse after emptying; `clear` preserving
  capacity; and a heap-owning element type with in-place `emplace_back`.

### `test_spsc_ring_buffer.cpp`
Exercises `SPSC_RingBuffer`. Oracle: a `std::queue` used as a sequential reference model.
Covers:
- single-threaded correctness: fill to capacity, rejection when full, FIFO drain, rejection
  when empty;
- wraparound: cycling the indices past the capacity many times;
- a model check: random push/pop against a `std::queue` that also enforces the capacity
  bound and FIFO order;
- a concurrent run: one producer and one consumer moving a large number of items (default
  1,000,000, override with `-DSPSC_N=...`), verifying in-order delivery with no gaps. The
  ThreadSanitizer build (`make tsan`) is what validates the acquire/release ordering.

### `test_shared_ptr.cpp`
Exercises `shared_ptr` and `weak_ptr`. The same templated suite runs against the hand-rolled
types and against `std::shared_ptr` / `std::weak_ptr`, so both must agree. Covers:
- ownership, copy, dereference, and shared mutation through aliased owners;
- copy assignment releasing the previous owner before adopting the new one;
- move semantics and safety of operating on a moved-from pointer;
- self copy-assign and self move-assign as no-ops;
- destruction through a base pointer running the derived destructor;
- `weak_ptr` lifecycle and `lock()`, including locking after the object is gone;
- `weak_ptr` copy and move (move must release the held reference, not leak it);
- a concurrent stress test where many threads copy, destroy, and `lock()` the same object.
