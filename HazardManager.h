#include <atomic>
#include <vector>

#ifndef HAZARD_MANAGER_H
#define HAZARD_MANAGER_H

template <typename T> class HazardManager {
  public:
    void mark_hazard(T *ptr) {
        m_hazard_pointers[get_index_for_thread()].store(ptr, std::memory_order_release);
    }

    void unmark_hazard() {
        m_hazard_pointers[get_index_for_thread()].store(nullptr, std::memory_order_release);
    }

    void retire(T* ptr) {
        if (nullptr == ptr){
            return;
        }

        std::vector<T *> &retired = m_retired_ptrs[get_index_for_thread()];
        retired.push_back(ptr);

        // If we are ready to reclaim memory
        if (retired.size() >= m_retired_limit) {
            reclaim_memory(retired);
        }
    }

    HazardManager(size_t max_thread, size_t retired_limit)
        : m_max_thread(max_thread), m_retired_limit(retired_limit), m_hazard_pointers(max_thread),
          m_retired_ptrs(max_thread) {
        for (std::atomic<T *> &ptr : m_hazard_pointers) {
            ptr = nullptr;
        }
    }

    HazardManager &operator=(HazardManager &&) = default;
    HazardManager(HazardManager &&) = default;
    HazardManager &operator=(HazardManager &) = delete;
    HazardManager(HazardManager &) = delete;

    // Assume object is destoryed only after all threads are done
    ~HazardManager() {
        for (std::vector<T *>& thread_retired_ptr : m_retired_ptrs){
            reclaim_memory(thread_retired_ptr);
        }
    }

  private:
    size_t get_index_for_thread() {
        thread_local int index = -1;
        if (index == -1) {
            int new_index = m_thread_id_counter.fetch_add(1);
            if (new_index >= m_max_thread) {
                throw std::runtime_error("Exceeded maximum number of hazard pointers");
            }
            index = new_index;
        }
        return index;
    }

    void reclaim_memory(std::vector<T *> &retired) {

        std::vector<T *> keep;
        keep.reserve(retired.size());
        for (T *ptr : retired) {
            bool to_keep = false;
            for (size_t index = 0; index < m_max_thread; index++) {
                if (m_hazard_pointers[index].load(std::memory_order_acquire) == ptr) {
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

    const size_t m_max_thread;
    const size_t m_retired_limit;
    std::atomic<size_t> m_thread_id_counter = 0;
    std::vector<std::vector<T *>> m_retired_ptrs;
    std::vector<std::atomic<T *>> m_hazard_pointers;
};

#endif