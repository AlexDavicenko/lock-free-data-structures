#include <atomic>
#include <thread>
#include <vector>

template <typename T> class HazardManager {
  public:
    void mark_as_hazard(T *ptr) {
        m_hp_array[get_hp_index_for_thread()].store(ptr, std::memory_order_release);
    }

    void unmark_hazard(T *ptr) {
        m_hp_array[get_hp_index_for_thread()].store(nullptr, std::memory_order_release);
    }

    void retire(T *ptr) {
        m_hp_array[get_hp_index_for_thread()].store(nullptr, std::memory_order_release);

        thread_local std::vector<T *> retired;
        retired.push_back(ptr);

        // If we are ready to reclaim memory
        if (retired.size() >= m_retired_limit) {
            std::vector<T *> keep;
            keep.reserve(retired.size());
            for (T *ptr : retired) {
                bool to_keep = false;
                for (size_t index = 0; index < m_max_thread; index++) {
                    if (m_hp_array[index].load(std::memory_order_acquire) == ptr) {
                        to_keep = true;
                        break;
                    }
                }

                if (to_keep) {
                    keep.push_back(ptr);
                } else {
                    // Reclaim memory
                    delete ptr;
                }
            }

            retired = std::move(keep);
        }
    }
    HazardManager(size_t max_thread, size_t retired_limit) : m_max_thread(max_thread), m_retired_limit(retired_limit) {
        m_hp_array = new std::atomic<T *>[max_thread];
        for (size_t i = 0; i < max_thread; i++) {
            m_hp_array[i].store(nullptr, std::memory_order_relaxed);
        }
    }

    HazardManager &operator=(HazardManager &&) = default;
    HazardManager(HazardManager &&) = default;
    HazardManager &operator=(HazardManager &) = delete;
    HazardManager(HazardManager &) = delete;

    ~HazardManager() {
        delete[] m_hp_array;
    }

  private:
    size_t get_hp_index_for_thread() {
        thread_local int index = -1;
        if (index == -1) {
            index = m_hp_counter.fetch_add(1);
            if (index >= m_max_thread) {
                throw std::runtime_error("Exceeded maximum number of hazard pointers");
            }
        }
        return index;
    }

    const size_t m_max_thread;
    const size_t m_retired_limit;
    std::atomic<size_t> m_hp_counter = 0;
    std::atomic<T *> *m_hp_array;
};
