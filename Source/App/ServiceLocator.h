// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

/**
 * @file ServiceLocator.h
 * @brief B-003: Service locator for global services
 *
 * Provides a centralized registry for application-wide services, replacing
 * scattered global singletons with a single point of access that can be
 * mocked for testing.
 *
 * USAGE:
 *   // Registration (at startup):
 *   ServiceLocator::provide<IAudioEngine>(myAudioEngine);
 *
 *   // Access (anywhere):
 *   auto* audio = ServiceLocator::get<IAudioEngine>();
 *   if (audio) audio->play();
 *
 * THREAD SAFETY:
 * - Registration should only happen on MAIN THREAD during startup
 * - Lookups are thread-safe (read-only after startup)
 * - Services themselves must be thread-safe if accessed from multiple threads
 *
 * DESIGN NOTES:
 * - Keys are per-type static tag addresses, not typeid, so this header stays
 *   valid under -fno-rtti (newer GCC rejects typeid in template bodies even
 *   uninstantiated). Tag addresses are unique per type within a single
 *   binary, which is all Aestra needs (statically linked, no services
 *   registered across shared-library boundaries).
 * - Services are stored as void* to allow any type
 * - Lifetime management is external (caller owns services)
 */

#include <memory>
#include <mutex>
#include <unordered_map>

namespace Aestra {

/**
 * @brief B-003: Minimal service locator
 * 
 * Not over-abstracted - just a typed registry for shared services.
 */
class ServiceLocator {
public:
    /**
     * @brief Register a service instance
     * @tparam T Service interface type
     * @param service Pointer to service (ownership NOT transferred)
     * 
     * Thread Safety: Call only from MAIN THREAD during startup.
     */
    template<typename T>
    static void provide(T* service) {
        std::lock_guard<std::mutex> lock(getMutex());
        getRegistry()[serviceKey<T>()] = static_cast<void*>(service);
    }
    
    /**
     * @brief Register a shared_ptr service
     * @tparam T Service interface type
     * @param service Shared pointer to service
     * 
     * Stores the raw pointer but you retain the shared_ptr for lifetime.
     */
    template<typename T>
    static void provide(std::shared_ptr<T> service) {
        provide<T>(service.get());
    }
    
    /**
     * @brief Get a service instance
     * @tparam T Service interface type
     * @return Pointer to service, or nullptr if not registered
     * 
     * Thread Safety: Safe from any thread after startup completes.
     */
    template<typename T>
    static T* get() {
        std::lock_guard<std::mutex> lock(getMutex());
        auto it = getRegistry().find(serviceKey<T>());
        if (it != getRegistry().end()) {
            return static_cast<T*>(it->second);
        }
        return nullptr;
    }
    
    /**
     * @brief Check if a service is registered
     */
    template<typename T>
    static bool has() {
        return get<T>() != nullptr;
    }
    
    /**
     * @brief Remove a service registration
     * @tparam T Service interface type
     */
    template<typename T>
    static void remove() {
        std::lock_guard<std::mutex> lock(getMutex());
        getRegistry().erase(serviceKey<T>());
    }
    
    /**
     * @brief Clear all registrations (for shutdown/testing)
     */
    static void clear() {
        std::lock_guard<std::mutex> lock(getMutex());
        getRegistry().clear();
    }

private:
    // Unique key per service type: the address of a per-instantiation static.
    // RTTI-free equivalent of std::type_index for a statically linked binary.
    using ServiceKey = const void*;

    template <typename T> static ServiceKey serviceKey() {
        static const char s_tag = 0;
        return &s_tag;
    }

    static std::unordered_map<ServiceKey, void*>& getRegistry() {
        static std::unordered_map<ServiceKey, void*> s_registry;
        return s_registry;
    }

    static std::mutex& getMutex() {
        static std::mutex s_mutex;
        return s_mutex;
    }
};

// NOTE: AppContext struct can be added later if explicit DI is needed.
// For now, use ServiceLocator with fully-qualified types:
//   ServiceLocator::provide<Aestra::Audio::AudioEngine>(engine);
//   auto* engine = ServiceLocator::get<Aestra::Audio::AudioEngine>();

} // namespace Aestra
