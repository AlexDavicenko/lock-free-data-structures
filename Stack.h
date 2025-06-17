#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <thread>
#include <vector>

#include "HazardManager.h"

template <typename T> class Stack {
  public:
    void push(T val) {
        Node *new_top = new Node(val, nullptr);
        do {
            new_top->next = m_top.load(std::memory_order_acquire);
        } while (
            !m_top.compare_exchange_weak(new_top->next, new_top, std::memory_order_release, std::memory_order_relaxed));
        m_size.fetch_add(1);
    }
    std::optional<T> pop() {
        Node *top;
        do {
            do {
                top = m_top.load(std::memory_order_acquire);
                if (top == nullptr) {
                    return std::nullopt;
                }

                m_hazard_manager.mark_as_hazard(top);
            } while (top != m_top.load(std::memory_order_acquire));

        } while (!m_top.compare_exchange_weak(top, top->next, std::memory_order_release, std::memory_order_acquire));
        m_size.fetch_sub(1);
        T ret = top->data;
        m_hazard_manager.retire(top);
        return ret;
    }
    static Stack<T> create(size_t max_thread) {
        return Stack<T>(max_thread);
    }

    Stack(Stack &&) noexcept = default;
    Stack &operator=(Stack &&) noexcept = default;
    Stack(const Stack &) = delete;
    Stack &operator=(const Stack &) = delete;

    ~Stack() {
        while (Node *node = m_top.load()) {
            m_top.store(node->next);
            delete node;
        }
    }

  private:
    struct Node {
        T data;
        Node *next;
    };
    Stack(size_t max_thread) : m_hazard_manager(max_thread, 10) {
    }
    std::atomic<size_t> m_size = 0;
    std::atomic<Node *> m_top = nullptr;
    HazardManager<Node> m_hazard_manager;
};

inline void test_Stack() {
    constexpr uint64_t thread_count = 200;
    constexpr uint64_t iter_size = 50000;

    auto stack = Stack<int>::create(thread_count);
    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> actors;
    std::atomic<uint64_t> sum = 0;

    for (int i = 0; i < thread_count; i++) {
        actors.emplace_back(std::thread([&stack, iter_size, &sum, i]() {
            // iter_size number of pops
            // iter_size number of pushes
            int counter = 0;
            for (int j = 0; j < 2 * iter_size; j++) {
                // Must start with a push or the test hangs
                if (j % 2 == 0) {
                    stack.push(counter++);
                } else {
                    auto val = stack.pop();
                    while (!val) {
                        std::this_thread::yield();
                        val = stack.pop();
                    }
                    sum.fetch_add(val.value());
                }
            }
        }));
    }

    for (int i = 0; i < thread_count; i++) {
        actors[i].join();
    }

    std::cout << "sum: " << sum << std::endl;
    auto expected = thread_count * iter_size * (iter_size - 1) / 2;
    std::cout << "expected: " << expected << std::endl;
    assert(sum == expected);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "Elapsed time: " << duration << " milliseconds" << std::endl;
}