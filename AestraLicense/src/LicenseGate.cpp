#include "LicenseGate.h"

#include "AestraJSON.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <dpapi.h>
#include <intrin.h>
#include <sodium.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <Security/Security.h>
#include <sodium.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#else
#include <libsecret/secret.h>
#include <sodium.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#endif

namespace Aestra {
namespace License {

namespace {
constexpr int64_t kLeasePeriodSeconds = 604800;
constexpr size_t kSecretBoxNonceBytes = 24;
constexpr size_t kSecretBoxMacBytes = 16;
constexpr const char* kBackupFileName = "lease.bin";
constexpr const char* kServiceName = "com.aestrastudios.license";
constexpr const char* kAccountName = "lease";

struct LeaseRecord {
    std::string licenseId;
    std::string userId;
    LicenseTier tier = LicenseTier::Core;
    std::vector<std::string> plugins;
    std::vector<std::string> features;
    std::string deviceHash;
    int64_t issuedAt = 0;
    int64_t expiresAt = 0;
    std::string gracePolicy;
    int64_t revocationEpoch = 0;
};

struct DeviceFingerprint {
    std::array<std::string, 4> fieldDigests;
    std::string encoded;
};

struct GateState {
    std::once_flag initOnce;
    LicenseTier tier = LicenseTier::Core;
    int64_t expiresAt = 0;
    EntitlementProfile profile;
};

GateState& gateState() {
    static GateState state;
    return state;
}

int64_t nowSeconds() {
    return static_cast<int64_t>(std::time(nullptr));
}

std::string trimCopy(std::string value) {
    auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::string toLowerHex(const unsigned char* bytes, size_t size) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.resize(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out[i * 2] = kHex[(bytes[i] >> 4) & 0x0F];
        out[i * 2 + 1] = kHex[bytes[i] & 0x0F];
    }
    return out;
}

bool fromHex(const std::string& hex, std::vector<unsigned char>& out) {
    if ((hex.size() % 2) != 0) {
        return false;
    }
    out.clear();
    out.reserve(hex.size() / 2);
    auto decodeNibble = [](char ch) -> int {
        if (ch >= '0' && ch <= '9')
            return ch - '0';
        if (ch >= 'a' && ch <= 'f')
            return 10 + (ch - 'a');
        if (ch >= 'A' && ch <= 'F')
            return 10 + (ch - 'A');
        return -1;
    };
    for (size_t i = 0; i < hex.size(); i += 2) {
        const int hi = decodeNibble(hex[i]);
        const int lo = decodeNibble(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            out.clear();
            return false;
        }
        out.push_back(static_cast<unsigned char>((hi << 4) | lo));
    }
    return true;
}

std::string escapeJson(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char ch : value) {
        switch (ch) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += ch;
            break;
        }
    }
    return out;
}

std::string quoteJson(const std::string& value) {
    return "\"" + escapeJson(value) + "\"";
}

std::string tierToString(LicenseTier tier) {
    switch (tier) {
    case LicenseTier::Supporter:
        return "Supporter";
    case LicenseTier::Founder:
        return "Founder";
    case LicenseTier::Core:
    default:
        return "Core";
    }
}

bool tierFromString(const std::string& value, LicenseTier& tier) {
    if (value == "Core") {
        tier = LicenseTier::Core;
        return true;
    }
    if (value == "Supporter") {
        tier = LicenseTier::Supporter;
        return true;
    }
    if (value == "Founder") {
        tier = LicenseTier::Founder;
        return true;
    }
    return false;
}

MembershipTier toMembershipTier(LicenseTier tier) {
    switch (tier) {
    case LicenseTier::Supporter:
        return MembershipTier::Supporter;
    case LicenseTier::Founder:
        return MembershipTier::Founder;
    case LicenseTier::Core:
    default:
        return MembershipTier::Core;
    }
}

EntitlementProfile makeCoreProfile(EntitlementStatus status) {
    EntitlementProfile profile;
    profile.tier = MembershipTier::Core;
    profile.status = status;
    profile.offline = true;
    profile.verified = false;
    return profile;
}

std::chrono::system_clock::time_point timePointFromUnixSeconds(int64_t seconds) {
    return std::chrono::system_clock::time_point(std::chrono::seconds(seconds));
}

std::string stringArrayToJson(const std::vector<std::string>& values) {
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << quoteJson(values[i]);
    }
    out << "]";
    return out.str();
}

std::string canonicalizeLeasePayload(const LeaseRecord& lease) {
    std::ostringstream out;
    out << "{"
        << "\"license_id\":" << quoteJson(lease.licenseId) << ","
        << "\"user_id\":" << quoteJson(lease.userId) << ","
        << "\"tier\":" << quoteJson(tierToString(lease.tier)) << ","
        << "\"plugins\":" << stringArrayToJson(lease.plugins) << ","
        << "\"features\":" << stringArrayToJson(lease.features) << ","
        << "\"device_hash\":" << quoteJson(lease.deviceHash) << ","
        << "\"issued_at\":" << lease.issuedAt << ","
        << "\"expires_at\":" << lease.expiresAt << ","
        << "\"grace_policy\":" << quoteJson(lease.gracePolicy) << ","
        << "\"revocation_epoch\":" << lease.revocationEpoch << "}";
    return out.str();
}

bool parseStringArray(Aestra::JSON& json, const std::string& key, std::vector<std::string>& out) {
    if (!json.has(key) || !json[key].isArray()) {
        return false;
    }
    out.clear();
    for (const auto& entry : json[key].asArray()) {
        if (!entry.isString()) {
            return false;
        }
        out.push_back(entry.asString());
    }
    return true;
}

bool parseLeasePayload(const std::string& payload, LeaseRecord& out) {
    Aestra::JSON json = Aestra::JSON::parse(payload);
    if (!json.isObject()) {
        return false;
    }
    if (!json.has("license_id") || !json["license_id"].isString())
        return false;
    if (!json.has("user_id") || !json["user_id"].isString())
        return false;
    if (!json.has("tier") || !json["tier"].isString())
        return false;
    if (!json.has("device_hash") || !json["device_hash"].isString())
        return false;
    if (!json.has("issued_at") || !json["issued_at"].isNumber())
        return false;
    if (!json.has("expires_at") || !json["expires_at"].isNumber())
        return false;
    if (!json.has("grace_policy") || !json["grace_policy"].isString())
        return false;
    if (!json.has("revocation_epoch") || !json["revocation_epoch"].isNumber())
        return false;

    LeaseRecord lease;
    lease.licenseId = json["license_id"].asString();
    lease.userId = json["user_id"].asString();
    if (!tierFromString(json["tier"].asString(), lease.tier)) {
        return false;
    }
    if (!parseStringArray(json, "plugins", lease.plugins)) {
        return false;
    }
    if (!parseStringArray(json, "features", lease.features)) {
        return false;
    }
    lease.deviceHash = json["device_hash"].asString();
    lease.issuedAt = static_cast<int64_t>(json["issued_at"].asNumber());
    lease.expiresAt = static_cast<int64_t>(json["expires_at"].asNumber());
    lease.gracePolicy = json["grace_policy"].asString();
    lease.revocationEpoch = static_cast<int64_t>(json["revocation_epoch"].asNumber());

    if (lease.expiresAt != lease.issuedAt + kLeasePeriodSeconds) {
        return false;
    }

    out = std::move(lease);
    return true;
}

bool splitStoredLeaseBlob(const std::vector<unsigned char>& blob, std::string& payload,
                          std::vector<unsigned char>& signature) {
    payload.clear();
    signature.clear();
    if (blob.empty()) {
        return false;
    }
    const std::string token(blob.begin(), blob.end());
    const size_t splitPos = token.rfind('\n');
    if (splitPos == std::string::npos) {
        return false;
    }

    payload = token.substr(0, splitPos);
    const std::string signatureHex = trimCopy(token.substr(splitPos + 1));
    return fromHex(signatureHex, signature);
}

std::filesystem::path defaultDataDir() {
    if (const char* envDir = std::getenv("AESTRA_DATA_DIR")) {
        if (*envDir != '\0') {
            return std::filesystem::path(envDir);
        }
    }

#ifdef _WIN32
    if (const char* appData = std::getenv("LOCALAPPDATA")) {
        return std::filesystem::path(appData) / "AESTRA";
    }
    return std::filesystem::temp_directory_path() / "AESTRA";
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / "Library" / "Application Support" / "AESTRA";
    }
    return std::filesystem::temp_directory_path() / "AESTRA";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) {
        return std::filesystem::path(xdg) / "AESTRA";
    }
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".local" / "share" / "AESTRA";
    }
    return std::filesystem::temp_directory_path() / "AESTRA";
#endif
}

std::filesystem::path backupLeasePath() {
    return defaultDataDir() / "license" / kBackupFileName;
}

void ensureSodiumInitialized() {
    static std::once_flag sodiumOnce;
    std::call_once(sodiumOnce, []() {
        const int rc = sodium_init();
        (void)rc;
    });
}

std::string hashTextBlake2bHex(const std::string& text, size_t outBytes = 16) {
#ifdef _WIN32
    return text;
#else
    ensureSodiumInitialized();
    std::vector<unsigned char> digest(outBytes, 0);
    crypto_generichash(digest.data(), digest.size(), reinterpret_cast<const unsigned char*>(text.data()), text.size(),
                       nullptr, 0);
    return toLowerHex(digest.data(), digest.size());
#endif
}

std::string linuxCpuVendor() {
#ifdef __linux__
    std::ifstream cpuInfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuInfo, line)) {
        if (line.rfind("vendor_id", 0) == 0) {
            const size_t pos = line.find(':');
            if (pos != std::string::npos) {
                return trimCopy(line.substr(pos + 1));
            }
        }
    }
#endif
    return "unknown-vendor";
}

std::string linuxCpuModel() {
#ifdef __linux__
    std::ifstream cpuInfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuInfo, line)) {
        if (line.rfind("model name", 0) == 0) {
            const size_t pos = line.find(':');
            if (pos != std::string::npos) {
                return trimCopy(line.substr(pos + 1));
            }
        }
    }
#endif
    return "unknown-model";
}

std::string platformString() {
#ifdef _WIN32
    OSVERSIONINFOEXA version{};
    version.dwOSVersionInfoSize = sizeof(version);
    GetVersionExA(reinterpret_cast<OSVERSIONINFOA*>(&version));
    std::ostringstream out;
    out << "windows-" << version.dwMajorVersion << "." << version.dwMinorVersion << "." << version.dwBuildNumber;
    return out.str();
#else
    struct utsname info{};
    if (uname(&info) == 0) {
        return std::string(info.sysname) + "-" + info.release;
    }
    return "unknown-platform";
#endif
}

std::string cpuVendorString() {
#ifdef _WIN32
    int cpuInfo[4] = {0, 0, 0, 0};
    __cpuid(cpuInfo, 0);
    char vendor[13] = {};
    std::memcpy(vendor + 0, &cpuInfo[1], 4);
    std::memcpy(vendor + 4, &cpuInfo[3], 4);
    std::memcpy(vendor + 8, &cpuInfo[2], 4);
    return std::string(vendor);
#elif defined(__APPLE__)
    char buffer[128] = {};
    size_t size = sizeof(buffer);
    if (sysctlbyname("machdep.cpu.vendor", buffer, &size, nullptr, 0) == 0 && size > 0) {
        return std::string(buffer, strnlen(buffer, sizeof(buffer)));
    }
    return "unknown-vendor";
#else
    return linuxCpuVendor();
#endif
}

std::string cpuModelString() {
#ifdef _WIN32
    HKEY key = nullptr;
    char buffer[256] = {};
    DWORD bufferSize = sizeof(buffer);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &key) ==
        ERROR_SUCCESS) {
        const LONG result = RegQueryValueExA(key, "ProcessorNameString", nullptr, nullptr,
                                             reinterpret_cast<LPBYTE>(buffer), &bufferSize);
        RegCloseKey(key);
        if (result == ERROR_SUCCESS) {
            return std::string(buffer, strnlen(buffer, sizeof(buffer)));
        }
    }
    return "unknown-model";
#elif defined(__APPLE__)
    char buffer[256] = {};
    size_t size = sizeof(buffer);
    if (sysctlbyname("machdep.cpu.brand_string", buffer, &size, nullptr, 0) == 0 && size > 0) {
        return std::string(buffer, strnlen(buffer, sizeof(buffer)));
    }
    return "unknown-model";
#else
    return linuxCpuModel();
#endif
}

std::string primaryVolumeSerialString() {
#ifdef _WIN32
    DWORD serial = 0;
    if (GetVolumeInformationA("C:\\", nullptr, 0, &serial, nullptr, nullptr, nullptr, 0)) {
        std::ostringstream out;
        out << std::hex << serial;
        return out.str();
    }
    return "unknown-volume";
#elif defined(__APPLE__)
    struct statfs stats{};
    if (statfs("/", &stats) == 0) {
        std::ostringstream out;
        out << std::hex << stats.f_fsid.val[0] << "-" << stats.f_fsid.val[1];
        return out.str();
    }
    return "unknown-volume";
#else
    struct stat stats{};
    if (stat("/", &stats) == 0) {
        std::ostringstream out;
        out << std::hex << static_cast<unsigned long long>(stats.st_dev);
        return out.str();
    }
    return "unknown-volume";
#endif
}

DeviceFingerprint collectDeviceFingerprint() {
    DeviceFingerprint fingerprint;
    const std::array<std::string, 4> rawFields = {
        cpuVendorString(),
        cpuModelString(),
        platformString(),
        primaryVolumeSerialString(),
    };

    for (size_t i = 0; i < rawFields.size(); ++i) {
        fingerprint.fieldDigests[i] = hashTextBlake2bHex(rawFields[i], 16);
    }

    std::ostringstream out;
    out << "v1";
    for (const auto& digest : fingerprint.fieldDigests) {
        out << ":" << digest;
    }
    fingerprint.encoded = out.str();
    return fingerprint;
}

bool deviceHashMatchesWithSingleFieldDrift(const std::string& stored, const DeviceFingerprint& current) {
    std::vector<std::string> storedParts;
    std::stringstream stream(stored);
    std::string item;
    while (std::getline(stream, item, ':')) {
        storedParts.push_back(item);
    }
    if (storedParts.size() != 5 || storedParts[0] != "v1") {
        return false;
    }

    int mismatches = 0;
    for (size_t i = 0; i < current.fieldDigests.size(); ++i) {
        if (storedParts[i + 1] != current.fieldDigests[i]) {
            ++mismatches;
        }
    }
    return mismatches <= 1;
}

std::vector<unsigned char> deriveBackupKey(const DeviceFingerprint& fingerprint) {
    std::vector<unsigned char> key(32, 0);
#ifdef _WIN32
    std::string encoded = fingerprint.encoded;
    for (size_t i = 0; i < key.size() && i < encoded.size(); ++i) {
        key[i] = static_cast<unsigned char>(encoded[i]);
    }
#else
    ensureSodiumInitialized();
    crypto_generichash(key.data(), key.size(), reinterpret_cast<const unsigned char*>(fingerprint.encoded.data()),
                       fingerprint.encoded.size(), nullptr, 0);
#endif
    return key;
}

#ifdef _WIN32
std::vector<unsigned char> loadLeaseFromPrimarySecretStore() {
    std::filesystem::path path = defaultDataDir() / "license" / "lease.dpapi";
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) {
        return {};
    }
    const std::vector<unsigned char> encrypted((std::istreambuf_iterator<char>(file)),
                                               std::istreambuf_iterator<char>());
    if (encrypted.empty()) {
        return {};
    }

    DATA_BLOB inBlob{};
    inBlob.pbData = const_cast<BYTE*>(encrypted.data());
    inBlob.cbData = static_cast<DWORD>(encrypted.size());

    DATA_BLOB outBlob{};
    if (!CryptUnprotectData(&inBlob, nullptr, nullptr, nullptr, nullptr, 0, &outBlob)) {
        return {};
    }

    std::vector<unsigned char> result(outBlob.pbData, outBlob.pbData + outBlob.cbData);
    LocalFree(outBlob.pbData);
    return result;
}
#elif defined(__APPLE__)
std::vector<unsigned char> loadLeaseFromPrimarySecretStore() {
    const std::string service(kServiceName);
    const std::string account(kAccountName);
    UInt32 length = 0;
    void* passwordData = nullptr;

    const OSStatus status = SecKeychainFindGenericPassword(nullptr, static_cast<UInt32>(service.size()),
                                                           service.c_str(), static_cast<UInt32>(account.size()),
                                                           account.c_str(), &length, &passwordData, nullptr);
    if (status != errSecSuccess || passwordData == nullptr) {
        if (passwordData != nullptr) {
            SecKeychainItemFreeContent(nullptr, passwordData);
        }
        return {};
    }
    if (length == 0) {
        SecKeychainItemFreeContent(nullptr, passwordData);
        return {};
    }

    std::vector<unsigned char> leaseBytes(static_cast<const unsigned char*>(passwordData),
                                          static_cast<const unsigned char*>(passwordData) + length);
    SecKeychainItemFreeContent(nullptr, passwordData);
    return leaseBytes;
}
#else
std::vector<unsigned char> loadLeaseFromPrimarySecretStore() {
    static const SecretSchema schema = {
        "com.aestrastudios.license",
        SECRET_SCHEMA_NONE,
        {
            {"account", SECRET_SCHEMA_ATTRIBUTE_STRING},
            {"NULL", SECRET_SCHEMA_ATTRIBUTE_STRING},
        },
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
    };

    GError* error = nullptr;
    gchar* secret = secret_password_lookup_sync(&schema, nullptr, &error, "account", kAccountName, nullptr);
    if (error != nullptr) {
        g_error_free(error);
        return {};
    }
    if (secret == nullptr) {
        return {};
    }

    const size_t length = std::strlen(secret);
    std::vector<unsigned char> bytes(reinterpret_cast<unsigned char*>(secret),
                                     reinterpret_cast<unsigned char*>(secret) + length);
    secret_password_free(secret);
    return bytes;
}
#endif

std::vector<unsigned char> loadLeaseFromEncryptedBackup(const DeviceFingerprint& fingerprint) {
    const std::filesystem::path path = backupLeasePath();
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) {
        return {};
    }

    const std::vector<unsigned char> encrypted((std::istreambuf_iterator<char>(file)),
                                               std::istreambuf_iterator<char>());
    if (encrypted.size() <= (kSecretBoxNonceBytes + kSecretBoxMacBytes)) {
        return {};
    }

#ifdef _WIN32
    return {};
#else
    ensureSodiumInitialized();
    const auto key = deriveBackupKey(fingerprint);
    const unsigned char* nonce = encrypted.data();
    const unsigned char* cipher = encrypted.data() + kSecretBoxNonceBytes;
    const size_t cipherSize = encrypted.size() - kSecretBoxNonceBytes;

    std::vector<unsigned char> plain(cipherSize - kSecretBoxMacBytes, 0);
    if (crypto_secretbox_open_easy(plain.data(), cipher, cipherSize, nonce, key.data()) != 0) {
        return {};
    }
    return plain;
#endif
}

bool verifyLeaseSignature(const std::string& payload, const std::vector<unsigned char>& signature) {
    return verifyEd25519Detached(payload, signature, AESTRA_LICENSE_PUBKEY);
}

EntitlementStatus loadAndVerifyLease(LeaseRecord& outLease) {
    const DeviceFingerprint fingerprint = collectDeviceFingerprint();

    std::vector<unsigned char> blob = loadLeaseFromPrimarySecretStore();
    if (blob.empty()) {
        blob = loadLeaseFromEncryptedBackup(fingerprint);
    }
    if (blob.empty()) {
        return EntitlementStatus::Missing;
    }

    std::string payload;
    std::vector<unsigned char> signature;
    if (!splitStoredLeaseBlob(blob, payload, signature)) {
        return EntitlementStatus::ParseError;
    }
    LeaseRecord lease;
    try {
        if (!parseLeasePayload(payload, lease)) {
            return EntitlementStatus::ParseError;
        }
    } catch (const std::exception&) {
        return EntitlementStatus::ParseError;
    }
    if (!verifyLeaseSignature(canonicalizeLeasePayload(lease), signature)) {
        return EntitlementStatus::InvalidSignature;
    }

    if (!deviceHashMatchesWithSingleFieldDrift(lease.deviceHash, fingerprint)) {
        return EntitlementStatus::WrongDevice;
    }
    if (lease.gracePolicy != "restrict") {
        return EntitlementStatus::ParseError;
    }
    if (lease.revocationEpoch != 0) {
        return EntitlementStatus::Revoked;
    }

    const int64_t now = nowSeconds();
    if (now > (lease.expiresAt + kLeasePeriodSeconds)) {
        return EntitlementStatus::Expired;
    }

    const EntitlementStatus status = now > lease.expiresAt ? EntitlementStatus::Grace : EntitlementStatus::Valid;
    outLease = std::move(lease);
    return status;
}

bool canAccessForTier(LicenseTier tier, Feature feature) {
    switch (feature) {
    case Feature::RUMBLE:
        return tier == LicenseTier::Supporter || tier == LicenseTier::Founder;
    case Feature::RUMBLE_HEADLESS:
        return tier == LicenseTier::Founder;
    default:
        return false;
    }
}

void initializeImpl() {
    LeaseRecord lease;
    GateState& state = gateState();
    const EntitlementStatus status = loadAndVerifyLease(lease);
    if (status == EntitlementStatus::Valid || status == EntitlementStatus::Grace) {
        state.tier = lease.tier;
        state.expiresAt = lease.expiresAt;
        state.profile.tier = toMembershipTier(lease.tier);
        state.profile.status = status;
        state.profile.userId = lease.userId;
        state.profile.licenseId = lease.licenseId;
        state.profile.rawPlugins = lease.plugins;
        state.profile.rawFeatures = lease.features;
        state.profile.issuedAt = timePointFromUnixSeconds(lease.issuedAt);
        state.profile.expiresAt = timePointFromUnixSeconds(lease.expiresAt);
        state.profile.offline = true;
        state.profile.verified = true;
    } else {
        state.tier = LicenseTier::Core;
        state.expiresAt = 0;
        state.profile = makeCoreProfile(status);
    }
    LicenseGate::refreshAsync();
}
} // namespace

bool verifyEd25519Detached(const std::string& payload, const std::vector<unsigned char>& signature,
                           const unsigned char publicKey[32]) {
    if (publicKey == nullptr || signature.size() != crypto_sign_BYTES) {
        return false;
    }
    ensureSodiumInitialized();
    return crypto_sign_verify_detached(signature.data(), reinterpret_cast<const unsigned char*>(payload.data()),
                                       payload.size(), publicKey) == 0;
}

void LicenseGate::initialize() {
    std::call_once(gateState().initOnce, initializeImpl);
}

LicenseTier LicenseGate::currentTier() {
    initialize();
    return gateState().tier;
}

EntitlementProfile LicenseGate::currentProfile() {
    initialize();
    return gateState().profile;
}

bool LicenseGate::canAccess(Feature feature) {
    initialize();
    return canAccessForTier(gateState().tier, feature);
}

void LicenseGate::refreshAsync() {}

int64_t LicenseGate::secondsUntilExpiry() {
    initialize();
    const int64_t expiresAt = gateState().expiresAt;
    if (expiresAt <= 0) {
        return 0;
    }
    return std::max<int64_t>(0, expiresAt - nowSeconds());
}

} // namespace License
} // namespace Aestra
