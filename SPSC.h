#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <thread>

template <typename T> class SPSC {
  public:
    /*Enqueue can only modify m_tail*/
    bool enqueue(T val) {
        size_t tail = m_tail.load(std::memory_order_relaxed);
        size_t nxt = (tail + 1) % m_cap;
        // Queue is full
        if (nxt == m_head) {
            return false;
        }

        m_data[tail] = Element(val);
        m_tail.store(nxt, std::memory_order_relaxed);

        return true;
    }
    /*Dequeue can only modify m_head*/
    std::optional<T> dequeue() {
        size_t head = m_head.load(std::memory_order_relaxed);
        
        //Queue is empty
        if (head == m_tail.load(std::memory_order_relaxed)) {
            return std::nullopt;
        }

        T ret = m_data[head].data;
        m_head.store((head + 1) % m_cap, std::memory_order_relaxed);
        return ret;
    }
    static SPSC<T> create(size_t cap) {
        return SPSC<T>(cap);
    }

    SPSC(SPSC &&) noexcept = default;
    SPSC &operator=(SPSC &&) noexcept = default;
    SPSC(const SPSC &) = delete;
    SPSC &operator=(const SPSC &) = delete;

    ~SPSC() {
        delete[] m_data;
        m_data = nullptr;
    }

  private:
    struct alignas(64) Element {
        T data;
    };
    SPSC(size_t cap) : m_cap(cap + 1), m_head(0), m_tail(0) {
        m_data = new Element[m_cap];
    }
    Element *m_data;
    size_t m_cap;
    alignas(64) std::atomic<size_t> m_head;
    alignas(64) std::atomic<size_t> m_tail;
};

inline void test_SPSC() {
    auto queue = SPSC<int>::create(10);
    constexpr uint64_t ITERS = 1000000;
    auto start = std::chrono::high_resolution_clock::now();
    std::thread producer([&]() {
        for (int i = 0; i < ITERS; i++) {
            while (!queue.enqueue(i)) {
                std::this_thread::yield();
            }
        }
    });
    uint64_t sum = 0;

    std::thread consumer([&]() {
        for (int i = 0; i < ITERS; i++) {
            bool consumed = false;
            do {
                auto v = queue.dequeue();
                if (v) {
                    sum += static_cast<uint64_t>(v.value());
                    consumed = true;
                } else {
                    std::this_thread::yield();
                }
            } while (!consumed);
        }
    });

    producer.join();
    consumer.join();

    assert(sum == ITERS * (ITERS - 1) / 2);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    std::cout << "Elapsed time: " << duration << " microseconds" << std::endl;
}