#pragma once
#include <atomic>
#include <memory>
#include <utility>

#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
#define AESTRA_HAS_STD_ATOMIC_SHARED_PTR 1
#endif

#ifndef AESTRA_HAS_STD_ATOMIC_SHARED_PTR
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
#endif

namespace Aestra {
template <typename T>
class AtomicSharedPtr {
public:
    AtomicSharedPtr() = default;
    explicit AtomicSharedPtr(std::shared_ptr<T> ptr) : m_ptr(std::move(ptr)) {}
    ~AtomicSharedPtr() = default;
    AtomicSharedPtr(const AtomicSharedPtr&) = delete;
    AtomicSharedPtr& operator=(const AtomicSharedPtr&) = delete;
    std::shared_ptr<T> load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
#ifdef AESTRA_HAS_STD_ATOMIC_SHARED_PTR
        return m_ptr.load(order);
#else
        return std::atomic_load_explicit(&m_ptr, order);
#endif
    }
    void store(std::shared_ptr<T> desired, std::memory_order order = std::memory_order_seq_cst) noexcept {
#ifdef AESTRA_HAS_STD_ATOMIC_SHARED_PTR
        m_ptr.store(std::move(desired), order);
#else
        std::atomic_store_explicit(&m_ptr, std::move(desired), order);
#endif
    }
    std::shared_ptr<T> exchange(std::shared_ptr<T> desired,
                                std::memory_order order = std::memory_order_seq_cst) noexcept {
#ifdef AESTRA_HAS_STD_ATOMIC_SHARED_PTR
        return m_ptr.exchange(std::move(desired), order);
#else
        return std::atomic_exchange_explicit(&m_ptr, std::move(desired), order);
#endif
    }
    bool compare_exchange_strong(std::shared_ptr<T>& expected, std::shared_ptr<T> desired,
                                  std::memory_order success = std::memory_order_seq_cst,
                                  std::memory_order failure = std::memory_order_seq_cst) noexcept {
#ifdef AESTRA_HAS_STD_ATOMIC_SHARED_PTR
        return m_ptr.compare_exchange_strong(expected, std::move(desired), success, failure);
#else
        return std::atomic_compare_exchange_strong_explicit(&m_ptr, &expected, std::move(desired), success, failure);
#endif
    }
private:
#ifdef AESTRA_HAS_STD_ATOMIC_SHARED_PTR
    std::atomic<std::shared_ptr<T>> m_ptr;
#else
    std::shared_ptr<T> m_ptr;
#endif
};
} // namespace Aestra

#ifndef AESTRA_HAS_STD_ATOMIC_SHARED_PTR
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
#endif

#undef AESTRA_HAS_STD_ATOMIC_SHARED_PTR
