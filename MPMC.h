#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

template <typename T> class MPMC {
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
        size_t head = m_head.fetch_add(1, std::memory_order_acq_rel);
        size_t index = head % m_cap;

        // Waiting for value to be produced
        while (m_data[index].m_seq.load(std::memory_order_acquire) != head + 1) {
            std::this_thread::yield();
        }

        T ret = m_data[index].m_value;
        // Store seq number head + N for next enqueue
        m_data[index].m_seq.store(head + m_cap, std::memory_order_release);
        return ret;
    }
    static MPMC<T> create(size_t cap) {
        return MPMC<T>(cap);
    }

    MPMC(MPMC &&) noexcept = default;
    MPMC &operator=(MPMC &&) noexcept = default;
    MPMC(const MPMC &) = delete;
    MPMC &operator=(const MPMC &) = delete;

    ~MPMC() {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (size_t i = 0; i < m_cap; ++i) {
                std::destroy_at(&m_data[i]);
            }
        }
        delete[] m_data;
        m_data = nullptr;
    }

  private:
    struct alignas(64) Element {
        std::atomic<size_t> m_seq;
        T m_value;
    };
    MPMC(size_t cap) : m_cap(cap + 1), m_head(0), m_tail(0) {
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

inline void test_MPMC() {
    auto mpmc = MPMC<uint64_t>::create(100);
    std::atomic<uint64_t> res = 0;

    constexpr uint64_t thread_count = 100;
    constexpr uint64_t iter_size = 5000;

    std::vector<std::thread> producers(thread_count);
    std::vector<std::thread> consumers(thread_count);

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < thread_count; i++) {
        producers[i] = std::thread([&mpmc]() {
            for (int j = 0; j < iter_size; j++) {
                mpmc.enqueue(j);
            }
        });
        consumers[i] = std::thread([&mpmc, &res]() {
            for (int j = 0; j < iter_size; j++) {
                res.fetch_add(mpmc.dequeue());
            }
        });
    }

    for (size_t i = 0; i < thread_count; i++) {
        producers[i].join();
        consumers[i].join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration = end - start;

    std::cout << "res: " << res << "\nans: " << thread_count * iter_size * (iter_size - 1) / 2
              << "\ntime: " << duration.count() << "\n";
}
