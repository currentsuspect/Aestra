// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "XdgPortalFileChooser.h"

#include <dlfcn.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <mutex>

namespace Aestra::XdgPortal {

namespace {

// ---------------------------------------------------------------------------
// Minimal libdbus-1 ABI, resolved at runtime. Only what the FileChooser round
// trip needs. DBusError's layout and the type codes are part of libdbus's
// frozen public ABI; DBusMessageIter is opaque stack storage, so an oversized
// aligned buffer stands in for it (libdbus's own is 72 bytes on 64-bit).
// ---------------------------------------------------------------------------

using dbus_bool_t = uint32_t;
struct DBusConnection;
struct DBusMessage;
struct DBusError {
    const char* name;
    const char* message;
    unsigned int dummy1 : 1;
    unsigned int dummy2 : 1;
    unsigned int dummy3 : 1;
    unsigned int dummy4 : 1;
    unsigned int dummy5 : 1;
    void* padding1;
};
struct DBusMessageIter {
    alignas(void*) unsigned char storage[128];
};

constexpr int kBusSession = 0;
constexpr int kTypeInvalid = 0;
constexpr int kTypeByte = 'y';
constexpr int kTypeBoolean = 'b';
constexpr int kTypeUint32 = 'u';
constexpr int kTypeString = 's';
constexpr int kTypeArray = 'a';
constexpr int kTypeVariant = 'v';
constexpr int kTypeStruct = 'r';
constexpr int kTypeDictEntry = 'e';

constexpr const char* kPortalBus = "org.freedesktop.portal.Desktop";
constexpr const char* kPortalPath = "/org/freedesktop/portal/desktop";
constexpr const char* kFileChooser = "org.freedesktop.portal.FileChooser";
constexpr const char* kRequest = "org.freedesktop.portal.Request";

struct DBusApi {
    void (*error_init)(DBusError*) = nullptr;
    void (*error_free)(DBusError*) = nullptr;
    dbus_bool_t (*error_is_set)(const DBusError*) = nullptr;
    dbus_bool_t (*threads_init_default)() = nullptr;
    DBusConnection* (*bus_get_private)(int, DBusError*) = nullptr;
    void (*connection_set_exit_on_disconnect)(DBusConnection*, dbus_bool_t) = nullptr;
    void (*connection_close)(DBusConnection*) = nullptr;
    void (*connection_unref)(DBusConnection*) = nullptr;
    const char* (*bus_get_unique_name)(DBusConnection*) = nullptr;
    void (*bus_add_match)(DBusConnection*, const char*, DBusError*) = nullptr;
    DBusMessage* (*message_new_method_call)(const char*, const char*, const char*, const char*) = nullptr;
    void (*message_unref)(DBusMessage*) = nullptr;
    void (*message_iter_init_append)(DBusMessage*, DBusMessageIter*) = nullptr;
    dbus_bool_t (*message_iter_append_basic)(DBusMessageIter*, int, const void*) = nullptr;
    dbus_bool_t (*message_iter_append_fixed_array)(DBusMessageIter*, int, const void*, int) = nullptr;
    dbus_bool_t (*message_iter_open_container)(DBusMessageIter*, int, const char*, DBusMessageIter*) = nullptr;
    dbus_bool_t (*message_iter_close_container)(DBusMessageIter*, DBusMessageIter*) = nullptr;
    DBusMessage* (*connection_send_with_reply_and_block)(DBusConnection*, DBusMessage*, int, DBusError*) = nullptr;
    dbus_bool_t (*connection_read_write)(DBusConnection*, int) = nullptr;
    DBusMessage* (*connection_pop_message)(DBusConnection*) = nullptr;
    dbus_bool_t (*message_is_signal)(DBusMessage*, const char*, const char*) = nullptr;
    const char* (*message_get_path)(DBusMessage*) = nullptr;
    dbus_bool_t (*message_iter_init)(DBusMessage*, DBusMessageIter*) = nullptr;
    int (*message_iter_get_arg_type)(DBusMessageIter*) = nullptr;
    void (*message_iter_get_basic)(DBusMessageIter*, void*) = nullptr;
    dbus_bool_t (*message_iter_next)(DBusMessageIter*) = nullptr;
    void (*message_iter_recurse)(DBusMessageIter*, DBusMessageIter*) = nullptr;
};

template <typename Fn>
bool resolve(void* lib, const char* name, Fn& out) {
    out = reinterpret_cast<Fn>(dlsym(lib, name));
    return out != nullptr;
}

// Loaded once, never unloaded (SDL may hold the same library).
const DBusApi* dbusApi() {
    static DBusApi api;
    static bool ok = false;
    static std::once_flag once;
    std::call_once(once, []() {
        void* lib = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
        if (!lib) return;
        ok = resolve(lib, "dbus_error_init", api.error_init) && resolve(lib, "dbus_error_free", api.error_free) &&
             resolve(lib, "dbus_error_is_set", api.error_is_set) &&
             resolve(lib, "dbus_threads_init_default", api.threads_init_default) &&
             resolve(lib, "dbus_bus_get_private", api.bus_get_private) &&
             resolve(lib, "dbus_connection_set_exit_on_disconnect", api.connection_set_exit_on_disconnect) &&
             resolve(lib, "dbus_connection_close", api.connection_close) &&
             resolve(lib, "dbus_connection_unref", api.connection_unref) &&
             resolve(lib, "dbus_bus_get_unique_name", api.bus_get_unique_name) &&
             resolve(lib, "dbus_bus_add_match", api.bus_add_match) &&
             resolve(lib, "dbus_message_new_method_call", api.message_new_method_call) &&
             resolve(lib, "dbus_message_unref", api.message_unref) &&
             resolve(lib, "dbus_message_iter_init_append", api.message_iter_init_append) &&
             resolve(lib, "dbus_message_iter_append_basic", api.message_iter_append_basic) &&
             resolve(lib, "dbus_message_iter_append_fixed_array", api.message_iter_append_fixed_array) &&
             resolve(lib, "dbus_message_iter_open_container", api.message_iter_open_container) &&
             resolve(lib, "dbus_message_iter_close_container", api.message_iter_close_container) &&
             resolve(lib, "dbus_connection_send_with_reply_and_block", api.connection_send_with_reply_and_block) &&
             resolve(lib, "dbus_connection_read_write", api.connection_read_write) &&
             resolve(lib, "dbus_connection_pop_message", api.connection_pop_message) &&
             resolve(lib, "dbus_message_is_signal", api.message_is_signal) &&
             resolve(lib, "dbus_message_get_path", api.message_get_path) &&
             resolve(lib, "dbus_message_iter_init", api.message_iter_init) &&
             resolve(lib, "dbus_message_iter_get_arg_type", api.message_iter_get_arg_type) &&
             resolve(lib, "dbus_message_iter_get_basic", api.message_iter_get_basic) &&
             resolve(lib, "dbus_message_iter_next", api.message_iter_next) &&
             resolve(lib, "dbus_message_iter_recurse", api.message_iter_recurse);
        // Pickers run on worker threads; libdbus needs its lock functions set.
        if (ok) ok = api.threads_init_default() != 0;
    });
    return ok ? &api : nullptr;
}

// One private session-bus connection per request: SDL shares the default
// connection for its own use, and a private one lets us close it cleanly.
class Connection {
public:
    explicit Connection(const DBusApi& api) : m_api(api) {
        DBusError err{};
        m_api.error_init(&err);
        m_conn = m_api.bus_get_private(kBusSession, &err);
        if (m_api.error_is_set(&err)) m_api.error_free(&err);
        // libdbus's default is to _exit() the process when the bus goes away.
        if (m_conn) m_api.connection_set_exit_on_disconnect(m_conn, 0);
    }
    ~Connection() {
        if (!m_conn) return;
        m_api.connection_close(m_conn);
        m_api.connection_unref(m_conn);
    }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    DBusConnection* get() const { return m_conn; }

private:
    const DBusApi& m_api;
    DBusConnection* m_conn = nullptr;
};

struct MessageRef {
    const DBusApi& api;
    DBusMessage* msg;
    ~MessageRef() {
        if (msg) api.message_unref(msg);
    }
};

// --- a{sv} builders ---------------------------------------------------------

class OptionsWriter {
public:
    OptionsWriter(const DBusApi& api, DBusMessageIter* parent) : m_api(api), m_parent(parent) {
        m_api.message_iter_open_container(m_parent, kTypeArray, "{sv}", &m_dict);
    }
    ~OptionsWriter() { m_api.message_iter_close_container(m_parent, &m_dict); }

    void string(const char* key, const std::string& value) {
        entry(key, "s", [&](DBusMessageIter* v) {
            const char* s = value.c_str();
            m_api.message_iter_append_basic(v, kTypeString, &s);
        });
    }
    void boolean(const char* key, bool value) {
        entry(key, "b", [&](DBusMessageIter* v) {
            const dbus_bool_t b = value ? 1 : 0;
            m_api.message_iter_append_basic(v, kTypeBoolean, &b);
        });
    }
    // Portal paths travel as NUL-terminated byte arrays.
    void bytePath(const char* key, const std::string& path) {
        entry(key, "ay", [&](DBusMessageIter* v) {
            DBusMessageIter arr{};
            m_api.message_iter_open_container(v, kTypeArray, "y", &arr);
            const char* data = path.c_str();
            m_api.message_iter_append_fixed_array(&arr, kTypeByte, &data, static_cast<int>(path.size() + 1));
            m_api.message_iter_close_container(v, &arr);
        });
    }
    void filters(const std::vector<Filter>& filters) {
        if (filters.empty()) return;
        entry("filters", "a(sa(us))", [&](DBusMessageIter* v) {
            DBusMessageIter list{};
            m_api.message_iter_open_container(v, kTypeArray, "(sa(us))", &list);
            for (const auto& f : filters) {
                DBusMessageIter item{};
                m_api.message_iter_open_container(&list, kTypeStruct, nullptr, &item);
                const char* name = f.name.c_str();
                m_api.message_iter_append_basic(&item, kTypeString, &name);
                DBusMessageIter pats{};
                m_api.message_iter_open_container(&item, kTypeArray, "(us)", &pats);
                for (const auto& p : f.patterns) {
                    DBusMessageIter pat{};
                    m_api.message_iter_open_container(&pats, kTypeStruct, nullptr, &pat);
                    const uint32_t glob = 0;
                    m_api.message_iter_append_basic(&pat, kTypeUint32, &glob);
                    const char* ps = p.c_str();
                    m_api.message_iter_append_basic(&pat, kTypeString, &ps);
                    m_api.message_iter_close_container(&pats, &pat);
                }
                m_api.message_iter_close_container(&item, &pats);
                m_api.message_iter_close_container(&list, &item);
            }
            m_api.message_iter_close_container(v, &list);
        });
    }

private:
    template <typename Fn>
    void entry(const char* key, const char* signature, Fn&& writeValue) {
        DBusMessageIter e{};
        DBusMessageIter v{};
        m_api.message_iter_open_container(&m_dict, kTypeDictEntry, nullptr, &e);
        m_api.message_iter_append_basic(&e, kTypeString, &key);
        m_api.message_iter_open_container(&e, kTypeVariant, signature, &v);
        writeValue(&v);
        m_api.message_iter_close_container(&e, &v);
        m_api.message_iter_close_container(&m_dict, &e);
    }

    const DBusApi& m_api;
    DBusMessageIter* m_parent;
    DBusMessageIter m_dict{};
};

// FileChooser interface version, or 0 when the portal cannot be reached.
uint32_t fileChooserVersion(const DBusApi& api, DBusConnection* conn) {
    MessageRef call{api, api.message_new_method_call(kPortalBus, kPortalPath, "org.freedesktop.DBus.Properties", "Get")};
    if (!call.msg) return 0;
    DBusMessageIter args{};
    api.message_iter_init_append(call.msg, &args);
    const char* iface = kFileChooser;
    const char* prop = "version";
    api.message_iter_append_basic(&args, kTypeString, &iface);
    api.message_iter_append_basic(&args, kTypeString, &prop);

    DBusError err{};
    api.error_init(&err);
    // Generous: the portal is bus-activated and may be starting up.
    MessageRef reply{api, api.connection_send_with_reply_and_block(conn, call.msg, 25000, &err)};
    if (api.error_is_set(&err)) {
        api.error_free(&err);
        return 0;
    }
    if (!reply.msg) return 0;

    DBusMessageIter it{};
    DBusMessageIter variant{};
    if (!api.message_iter_init(reply.msg, &it) || api.message_iter_get_arg_type(&it) != kTypeVariant) return 0;
    api.message_iter_recurse(&it, &variant);
    if (api.message_iter_get_arg_type(&variant) != kTypeUint32) return 0;
    uint32_t version = 0;
    api.message_iter_get_basic(&variant, &version);
    return version;
}

// First entry of results["uris"], or "" if absent.
std::string firstUri(const DBusApi& api, DBusMessageIter* results) {
    DBusMessageIter dict{};
    api.message_iter_recurse(results, &dict);
    while (api.message_iter_get_arg_type(&dict) == kTypeDictEntry) {
        DBusMessageIter entry{};
        api.message_iter_recurse(&dict, &entry);
        const char* key = nullptr;
        if (api.message_iter_get_arg_type(&entry) == kTypeString) api.message_iter_get_basic(&entry, &key);
        if (key && std::string(key) == "uris" && api.message_iter_next(&entry) &&
            api.message_iter_get_arg_type(&entry) == kTypeVariant) {
            DBusMessageIter variant{};
            api.message_iter_recurse(&entry, &variant);
            if (api.message_iter_get_arg_type(&variant) == kTypeArray) {
                DBusMessageIter uris{};
                api.message_iter_recurse(&variant, &uris);
                if (api.message_iter_get_arg_type(&uris) == kTypeString) {
                    const char* uri = nullptr;
                    api.message_iter_get_basic(&uris, &uri);
                    return uri ? std::string(uri) : std::string();
                }
            }
        }
        api.message_iter_next(&dict);
    }
    return {};
}

std::string requestToken() {
    static std::atomic<uint32_t> counter{0};
    return "aestra" + std::to_string(static_cast<long>(getpid())) + "_" + std::to_string(++counter);
}

// Portal request object path for (unique name, token): ":1.42" -> "1_42".
std::string requestPath(const std::string& uniqueName, const std::string& token) {
    std::string sender = uniqueName;
    if (!sender.empty() && sender[0] == ':') sender.erase(0, 1);
    for (char& c : sender) {
        if (c == '.') c = '_';
    }
    return std::string(kPortalPath) + "/request/" + sender + "/" + token;
}

enum class Method { Open, Save };

struct Request {
    Method method = Method::Open;
    std::string title;
    bool directory = false;
    std::vector<Filter> filters;
    std::string suggestedPath; // Save only
};

std::optional<std::string> run(const Request& req) {
    const DBusApi* api = dbusApi();
    if (!api) return std::nullopt;

    Connection conn(*api);
    if (!conn.get()) return std::nullopt;

    const uint32_t version = fileChooserVersion(*api, conn.get());
    if (version == 0) return std::nullopt;
    if (req.directory && version < 3) return std::nullopt; // "directory" arrived in v3

    const char* unique = api->bus_get_unique_name(conn.get());
    if (!unique) return std::nullopt;
    const std::string token = requestToken();
    const std::string expectedPath = requestPath(unique, token);

    // Subscribe BEFORE the call so a fast answer cannot be missed. The private
    // connection only carries this request's traffic.
    {
        DBusError err{};
        api->error_init(&err);
        const std::string rule = std::string("type='signal',sender='") + kPortalBus + "',interface='" + kRequest +
                                 "',member='Response'";
        api->bus_add_match(conn.get(), rule.c_str(), &err);
        if (api->error_is_set(&err)) {
            api->error_free(&err);
            return std::nullopt;
        }
    }

    MessageRef call{*api, api->message_new_method_call(kPortalBus, kPortalPath, kFileChooser,
                                                       req.method == Method::Save ? "SaveFile" : "OpenFile")};
    if (!call.msg) return std::nullopt;
    {
        DBusMessageIter args{};
        api->message_iter_init_append(call.msg, &args);
        const char* parentWindow = "";
        const char* title = req.title.c_str();
        api->message_iter_append_basic(&args, kTypeString, &parentWindow);
        api->message_iter_append_basic(&args, kTypeString, &title);
        OptionsWriter options(*api, &args);
        options.string("handle_token", token);
        options.boolean("modal", true);
        if (req.method == Method::Open) {
            options.boolean("multiple", false);
            if (req.directory) options.boolean("directory", true);
        } else if (!req.suggestedPath.empty()) {
            const std::filesystem::path suggested(req.suggestedPath);
            if (suggested.has_filename()) options.string("current_name", suggested.filename().string());
            std::error_code ec;
            if (suggested.has_parent_path() && std::filesystem::is_directory(suggested.parent_path(), ec)) {
                options.bytePath("current_folder", suggested.parent_path().string());
            }
        }
        options.filters(req.filters);
    }

    std::string handlePath;
    {
        DBusError err{};
        api->error_init(&err);
        MessageRef reply{*api, api->connection_send_with_reply_and_block(conn.get(), call.msg, 25000, &err)};
        if (api->error_is_set(&err)) {
            std::cerr << "[Portal] FileChooser call failed: " << (err.message ? err.message : "?") << std::endl;
            api->error_free(&err);
            return std::nullopt;
        }
        if (!reply.msg) return std::nullopt;
        DBusMessageIter it{};
        if (api->message_iter_init(reply.msg, &it) && api->message_iter_get_arg_type(&it) == 'o') {
            const char* handle = nullptr;
            api->message_iter_get_basic(&it, &handle);
            if (handle) handlePath = handle;
        }
        // A reply with no request object confirms no dialog, and a Response on a
        // path we cannot predict would never match: fall back instead of waiting.
        if (handlePath.empty()) return std::nullopt;
    }

    // From here the dialog is on screen: every outcome is final (a result or a
    // cancel), never "unavailable" — falling back now would stack a second picker.
    while (true) {
        if (!api->connection_read_write(conn.get(), 250)) {
            return std::string(); // bus went away mid-dialog
        }
        while (DBusMessage* raw = api->connection_pop_message(conn.get())) {
            MessageRef msg{*api, raw};
            if (!api->message_is_signal(msg.msg, kRequest, "Response")) continue;
            const char* path = api->message_get_path(msg.msg);
            // Pre-0.9 portals ignored handle_token and returned their own path.
            if (!path || (expectedPath != path && handlePath != path)) continue;

            DBusMessageIter it{};
            if (!api->message_iter_init(msg.msg, &it) || api->message_iter_get_arg_type(&it) != kTypeUint32) {
                return std::string();
            }
            uint32_t response = 2;
            api->message_iter_get_basic(&it, &response);
            if (response != 0) return std::string(); // 1 = cancelled, 2 = ended otherwise
            if (!api->message_iter_next(&it) || api->message_iter_get_arg_type(&it) != kTypeArray) {
                return std::string();
            }
            return pathFromFileUri(firstUri(*api, &it));
        }
    }
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

std::string pathFromFileUri(const std::string& uri) {
    static const std::string kScheme = "file://";
    if (uri.compare(0, kScheme.size(), kScheme) != 0) return {};
    std::string rest = uri.substr(kScheme.size());
    // file:///abs or file://localhost/abs; any other host is not local.
    if (rest.compare(0, 9, "localhost") == 0) rest.erase(0, 9);
    if (rest.empty() || rest[0] != '/') return {};

    std::string path;
    path.reserve(rest.size());
    for (size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] == '%' && i + 2 < rest.size()) {
            const int hi = hexValue(rest[i + 1]);
            const int lo = hexValue(rest[i + 2]);
            if (hi >= 0 && lo >= 0) {
                path.push_back(static_cast<char>(hi * 16 + lo));
                i += 2;
                continue;
            }
        }
        path.push_back(rest[i]);
    }
    return path;
}

std::vector<Filter> parseWin32Filter(const std::string& filter) {
    std::vector<Filter> out;
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i < filter.size(); ++i) {
        if (filter[i] == '\0') {
            parts.push_back(filter.substr(start, i - start));
            start = i + 1;
        }
    }
    if (start < filter.size()) parts.push_back(filter.substr(start));

    for (size_t i = 0; i + 1 < parts.size(); i += 2) {
        if (parts[i].empty()) break;
        Filter f;
        f.name = parts[i];
        size_t p = 0;
        const std::string& spec = parts[i + 1];
        while (p <= spec.size()) {
            const size_t semi = spec.find(';', p);
            std::string pattern = spec.substr(p, semi == std::string::npos ? std::string::npos : semi - p);
            if (pattern == "*.*") pattern = "*";
            if (!pattern.empty()) f.patterns.push_back(pattern);
            if (semi == std::string::npos) break;
            p = semi + 1;
        }
        if (!f.patterns.empty()) out.push_back(std::move(f));
    }
    return out;
}

std::optional<std::string> openFile(const std::string& title, const std::vector<Filter>& filters) {
    Request req;
    req.method = Method::Open;
    req.title = title;
    req.filters = filters;
    return run(req);
}

std::optional<std::string> saveFile(const std::string& title, const std::vector<Filter>& filters,
                                    const std::string& suggestedPath) {
    Request req;
    req.method = Method::Save;
    req.title = title;
    req.filters = filters;
    req.suggestedPath = suggestedPath;
    return run(req);
}

std::optional<std::string> selectFolder(const std::string& title) {
    Request req;
    req.method = Method::Open;
    req.title = title;
    req.directory = true;
    return run(req);
}

} // namespace Aestra::XdgPortal
