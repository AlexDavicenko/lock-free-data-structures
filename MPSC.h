#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

template <typename T> class MPSC {
  public:
    void enqueue(T val) {
        // Use tail as the next ticket
        size_t ticket = m_tail.fetch_add(1, std::memory_order_relaxed);

        size_t index = ticket % m_cap;

        // Waiting for previous value to be consumed
        while (m_data[index].m_seq.load(std::memory_order_acquire) != ticket) {
            std::this_thread::yield();
        }

        m_data[index].m_value = std::move(val);
        // Increment after val only now has been moved
        m_data[index].m_seq.store(ticket + 1, std::memory_order_release);
    }

    T dequeue() {
        size_t head = m_head.load(std::memory_order_relaxed);
        size_t index = head % m_cap;

        // Waiting for value to be produced
        while (m_data[index].m_seq.load(std::memory_order_acquire) != head + 1) {
            std::this_thread::yield();
        }

        T ret = m_data[index].m_value;
        // Store seq number head + N for next enqueue
        m_data[index].m_seq.store(head + m_cap, std::memory_order_release);
        m_head.fetch_add(1, std::memory_order_relaxed);
        return ret;
    }
    static MPSC<T> create(size_t cap) {
        return MPSC<T>(cap);
    }

    MPSC(MPSC &&) noexcept = default;
    MPSC &operator=(MPSC &&) noexcept = default;
    MPSC(const MPSC &) = delete;
    MPSC &operator=(const MPSC &) = delete;

    ~MPSC() {
        for (size_t i = 0; i < m_cap; ++i) {
            std::destroy_at(&m_data[i]);
        }
        delete[] m_data;
        m_data = nullptr;
    }

  private:
    struct alignas(64) Element {
        std::atomic<size_t> m_seq;
        T m_value;
    };
    MPSC(size_t cap) : m_cap(cap + 1), m_head(0), m_tail(0) {
        m_data = new Element[m_cap];
        for (size_t i = 0; i < m_cap; i++) {
            std::construct_at(&m_data[i], i);
        }
    }
    Element *m_data;
    size_t m_cap;
    alignas(64) std::atomic<size_t> m_head;
    alignas(64) std::atomic<size_t> m_tail;
};

inline void test_MPSC() {
    auto mpsc = MPSC<uint64_t>::create(100);
    std::atomic<uint64_t> res = 0;

    constexpr uint64_t thread_count = 20;
    constexpr uint64_t iter_size = 5000;

    std::vector<std::thread> producers(thread_count);

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < thread_count; i++) {
        producers[i] = std::thread([&]() {
            for (int j = 0; j < iter_size; j++) {
                mpsc.enqueue(j);
            }
        });
    }
    std::thread consumer([&]() {
        for (int j = 0; j < thread_count * iter_size; j++) {
            res.fetch_add(mpsc.dequeue());
        }
    });

    for (size_t i = 0; i < thread_count; i++) {
        producers[i].join();
    }
    consumer.join();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration = end - start;

    std::cout << "res: " << res << "\nans: " << thread_count * iter_size * (iter_size - 1) / 2
              << "\ntime: " << duration.count() << "\n";
}
