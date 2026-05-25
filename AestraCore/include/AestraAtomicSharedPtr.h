#pragma once
#include <memory>
#include <utility>

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

namespace Aestra {
template <typename T>
class AtomicSharedPtr {
public:
    AtomicSharedPtr() = default;
    explicit AtomicSharedPtr(std::shared_ptr<T> ptr) : m_ptr(std::move(ptr)) {}
    ~AtomicSharedPtr() = default;
    AtomicSharedPtr(const AtomicSharedPtr&) = delete;
    AtomicSharedPtr& operator=(const AtomicSharedPtr&) = delete;
    std::shared_ptr<T> load() const noexcept { return std::atomic_load(&m_ptr); }
    void store(std::shared_ptr<T> desired) noexcept { std::atomic_store(&m_ptr, std::move(desired)); }
    std::shared_ptr<T> exchange(std::shared_ptr<T> desired) noexcept { return std::atomic_exchange(&m_ptr, std::move(desired)); }
    bool compare_exchange_strong(std::shared_ptr<T>& expected, std::shared_ptr<T> desired,
                                  std::memory_order success = std::memory_order_seq_cst,
                                  std::memory_order failure = std::memory_order_seq_cst) noexcept {
        return std::atomic_compare_exchange_strong_explicit(&m_ptr, &expected, std::move(desired), success, failure);
    }
private:
    std::shared_ptr<T> m_ptr;
};
} // namespace Aestra

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
