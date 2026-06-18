CXX      ?= g++
CXXSTD   := -std=c++17
WARN     := -Wall -Wextra -Wpedantic
OPT      := -O2

# Address + UB sanitizers for the manually-memory-managed types (vectors, shared_ptr).
SAN_AU   := -fsanitize=address,undefined -fno-omit-frame-pointer -g
# Thread sanitizer for code whose correctness rests on atomic ordering.
SAN_T    := -fsanitize=thread -g

VEC_DIR  := vec
SPSC_DIR := spsc
SHARED_PTR_DIR  := shared_ptr
TEST_DIR := test
BIN      := bin

INCLUDES := -I$(VEC_DIR) -I$(SPSC_DIR) -I$(SHARED_PTR_DIR) -I$(TEST_DIR)

VEC_HDRS  := $(VEC_DIR)/vector_allocator.h $(VEC_DIR)/vector_raw.h $(TEST_DIR)/test_support.h
SPSC_HDRS := $(SPSC_DIR)/spsc_ring_buffer.h $(TEST_DIR)/test_support.h
PTR_HDRS  := $(SHARED_PTR_DIR)/shared_ptr.h

.PHONY: all test tsan clean

all: $(BIN)/vector_test $(BIN)/spsc_ring_buffer_test $(BIN)/shared_ptr_test

$(BIN):
	mkdir -p $(BIN)

# Both Vector implementations in one TU
$(BIN)/vector_test: $(TEST_DIR)/test_vector.cpp $(VEC_HDRS) | $(BIN)
	$(CXX) $(CXXSTD) $(WARN) $(OPT) $(SAN_AU) $(INCLUDES) $< -o $@

$(BIN)/spsc_ring_buffer_test: $(TEST_DIR)/test_spsc_ring_buffer.cpp $(SPSC_HDRS) | $(BIN)
	$(CXX) $(CXXSTD) $(WARN) $(OPT) $(INCLUDES) -pthread $< -o $@

$(BIN)/shared_ptr_test: $(TEST_DIR)/test_shared_ptr.cpp $(PTR_HDRS) | $(BIN)
	$(CXX) $(CXXSTD) $(WARN) $(OPT) $(SAN_AU) $(INCLUDES) -pthread $< -o $@

# ---- Thread-sanitized builds (validate atomic acquire/release ordering) ----
# Smaller N so TSan instrumentation on the ring buffer stays fast.
$(BIN)/spsc_ring_buffer_tsan: $(TEST_DIR)/test_spsc_ring_buffer.cpp $(SPSC_HDRS) | $(BIN)
	$(CXX) $(CXXSTD) $(WARN) $(OPT) $(SAN_T) $(INCLUDES) -pthread -DSPSC_N=200000 $< -o $@

$(BIN)/shared_ptr_tsan: $(TEST_DIR)/test_shared_ptr.cpp $(PTR_HDRS) | $(BIN)
	$(CXX) $(CXXSTD) $(WARN) $(OPT) $(SAN_T) $(INCLUDES) -pthread $< -o $@

test: all
	$(BIN)/vector_test
	$(BIN)/spsc_ring_buffer_test
	$(BIN)/shared_ptr_test

tsan: $(BIN)/spsc_ring_buffer_tsan $(BIN)/shared_ptr_tsan
	$(BIN)/spsc_ring_buffer_tsan
	$(BIN)/shared_ptr_tsan

clean:
	rm -rf $(BIN)