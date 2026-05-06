// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <functional>
#include <vector>

namespace Aestra {
namespace Events {

/**
 * @brief Opaque token representing an active signal subscription.
 *
 * Connection is move-only and represents a single subscription to a Signal.
 * It can be disconnected explicitly or allowed to disconnect on destruction
 * when owned by a ScopedConnection.
 */
class Connection {
public:
    Connection() noexcept = default;
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&& other) noexcept = default;
    Connection& operator=(Connection&& other) noexcept = default;

    /** @brief Disconnect this subscription. Safe to call multiple times. */
    void disconnect() {
        if (m_disconnect) {
            m_disconnect();
            m_disconnect = nullptr;
        }
    }

    /** @brief Check if this connection is still active. */
    bool connected() const noexcept { return m_disconnect != nullptr; }

private:
    explicit Connection(std::function<void()> disconnect)
        : m_disconnect(std::move(disconnect)) {}

    std::function<void()> m_disconnect;

    template <typename> friend class Signal;
    friend class ScopedConnection;
};

/**
 * @brief Owns a Connection and disconnects it automatically on destruction.
 *
 * Use this for automatic RAII cleanup of signal subscriptions.
 */
class ScopedConnection {
public:
    ScopedConnection() noexcept = default;
    ScopedConnection(const ScopedConnection&) = delete;
    ScopedConnection& operator=(const ScopedConnection&) = delete;
    ScopedConnection(ScopedConnection&& other) noexcept
        : m_connection(std::move(other.m_connection)) {
        other.m_connection.disconnect();
    }
    ScopedConnection& operator=(ScopedConnection&& other) noexcept {
        if (this != &other) {
            disconnect();
            m_connection = std::move(other.m_connection);
            other.m_connection.disconnect();
        }
        return *this;
    }
    ~ScopedConnection() { disconnect(); }

    explicit ScopedConnection(Connection conn) noexcept
        : m_connection(std::move(conn)) {}

    /** @brief Release ownership without disconnecting. */
    void reset() noexcept { m_connection = Connection{}; }

    /** @brief Disconnect and assign new connection. */
    void reset(Connection conn) noexcept {
        disconnect();
        m_connection = std::move(conn);
    }

    /** @brief Disconnect the owned connection. */
    void disconnect() noexcept {
        if (m_connection.connected()) {
            m_connection.disconnect();
        }
    }

    /** @brief Check if the connection is active. */
    bool connected() const noexcept { return m_connection.connected(); }

private:
    Connection m_connection;
};

/**
 * @brief Container for multiple ScopedConnection instances.
 *
 * Automatically disconnects all connections on destruction.
 */
class ScopedConnections {
public:
    ScopedConnections() = default;
    ScopedConnections(const ScopedConnections&) = delete;
    ScopedConnections& operator=(const ScopedConnections&) = delete;
    ScopedConnections(ScopedConnections&&) = default;
    ScopedConnections& operator=(ScopedConnections&&) = default;

    /** @brief Add a connection to be managed. */
    void add(Connection conn) {
        if (conn.connected()) {
            m_connections.emplace_back(std::move(conn));
        }
    }

    /** @brief Disconnect all managed connections. */
    void disconnectAll() {
        for (auto& conn : m_connections) {
            conn.disconnect();
        }
        m_connections.clear();
    }

    ~ScopedConnections() { disconnectAll(); }

private:
    std::vector<ScopedConnection> m_connections;
};

/**
 * @brief Template signal for typed callback subscriptions.
 *
 * @tparam T Event argument type (use void for no-argument signals)
 */
template <typename T = void>
class Signal;

template <typename T>
class Signal {
public:
    using Callback = std::function<void(const T&)>;

    /** @brief Subscribe to the signal. Returns a Connection to disconnect. */
    Connection subscribe(Callback callback) {
        m_callbacks.push_back(std::move(callback));
        auto index = m_callbacks.size() - 1;
        return Connection([this, index]() {
            if (index < m_callbacks.size()) {
                m_callbacks[index] = nullptr;
            }
        });
    }

    /** @brief Emit the signal to all current subscribers. */
    void emit(const T& event) {
        for (auto& callback : m_callbacks) {
            if (callback) {
                callback(event);
            }
        }
    }

    /** @brief Emit without payload (for void T or default construction). */
    void emit() {
        for (auto& callback : m_callbacks) {
            if (callback) {
                callback(T{});
            }
        }
    }

#ifdef AESTRA_DEBUG
    /** @brief Get subscriber count (debug builds only). */
    size_t subscriberCount() const {
        size_t count = 0;
        for (const auto& cb : m_callbacks) {
            if (cb) ++count;
        }
        return count;
    }
#endif

private:
    std::vector<Callback> m_callbacks;
};

/** @brief Specialization for void signals (no event argument). */
template <>
class Signal<void> {
public:
    using Callback = std::function<void()>;

    Connection subscribe(Callback callback) {
        m_callbacks.push_back(std::move(callback));
        auto index = m_callbacks.size() - 1;
        return Connection([this, index]() {
            if (index < m_callbacks.size()) {
                m_callbacks[index] = nullptr;
            }
        });
    }

    void emit() {
        for (auto& callback : m_callbacks) {
            if (callback) {
                callback();
            }
        }
    }

#ifdef AESTRA_DEBUG
    size_t subscriberCount() const {
        size_t count = 0;
        for (const auto& cb : m_callbacks) {
            if (cb) ++count;
        }
        return count;
    }
#endif

private:
    std::vector<Callback> m_callbacks;
};

} // namespace Events
} // namespace Aestra