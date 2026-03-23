// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "ProjectValidator.h"
#include "AestraLog.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>
#include <set>

// Simple JSON validation helpers - in production, use a proper JSON library
// For now, we'll do basic structural validation

namespace Aestra {
namespace Audio {

//==============================================================================
// SchemaVersion
//==============================================================================

std::optional<ProjectValidator::SchemaVersion> 
ProjectValidator::SchemaVersion::fromString(const std::string& str) {
    std::regex versionRegex(R"((\d+)\.(\d+)\.(\d+))");
    std::smatch match;
    
    if (std::regex_match(str, match, versionRegex)) {
        SchemaVersion version;
        version.major = std::stoul(match[1]);
        version.minor = std::stoul(match[2]);
        version.patch = std::stoul(match[3]);
        return version;
    }
    
    // Try simple integer format
    try {
        size_t pos = 0;
        uint32_t major = std::stoul(str, &pos);
        if (pos == str.length()) {
            SchemaVersion version;
            version.major = major;
            return version;
        }
    } catch (...) {}
    
    return std::nullopt;
}

//==============================================================================
// Lifecycle
//==============================================================================

ProjectValidator::ProjectValidator(Config config)
    : m_config(std::move(config))
{
}

//==============================================================================
// Validation
//==============================================================================

ProjectValidator::ValidationResult ProjectValidator::validateFile(const std::string& filePath) const {
    ValidationResult result;
    
    // Check file exists
    if (!fileExists(filePath)) {
        addIssue(result, Severity::Error, "", 
                 "Project file not found: " + filePath,
                 "E_FILE_NOT_FOUND");
        return result;
    }
    
    // Check file size (not empty, not suspiciously large)
    try {
        auto size = std::filesystem::file_size(filePath);
        if (size == 0) {
            addIssue(result, Severity::Error, "",
                     "Project file is empty",
                     "E_EMPTY_FILE");
            return result;
        }
        if (size > 100 * 1024 * 1024) {  // 100MB limit
            addIssue(result, Severity::Warning, "",
                     "Project file is unusually large (" + 
                     std::to_string(size / (1024*1024)) + " MB)",
                     "W_LARGE_FILE");
        }
    } catch (const std::exception& e) {
        addIssue(result, Severity::Error, "",
                 "Cannot read file: " + std::string(e.what()),
                 "E_FILE_ACCESS");
        return result;
    }
    
    // Read file content
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        addIssue(result, Severity::Error, "",
                 "Cannot open project file for reading",
                 "E_FILE_OPEN");
        return result;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    return validateContent(content);
}

ProjectValidator::ValidationResult ProjectValidator::validateContent(const std::string& jsonContent) const {
    ValidationResult result;
    
    // Basic JSON structure check
    if (jsonContent.empty()) {
        addIssue(result, Severity::Error, "",
                 "Content is empty",
                 "E_EMPTY_CONTENT");
        return result;
    }
    
    // Check JSON starts with { or [
    size_t firstNonWs = jsonContent.find_first_not_of(" \t\n\r");
    if (firstNonWs == std::string::npos || 
        (jsonContent[firstNonWs] != '{' && jsonContent[firstNonWs] != '[')) {
        addIssue(result, Severity::Error, "",
                 "Content is not valid JSON (must start with { or [)",
                 "E_INVALID_JSON");
        return result;
    }
    
    // Check for balanced braces (basic structural validation)
    int braceCount = 0;
    int bracketCount = 0;
    bool inString = false;
    bool escaped = false;
    
    for (char c : jsonContent) {
        if (escaped) {
            escaped = false;
            continue;
        }
        
        if (c == '\\' && inString) {
            escaped = true;
            continue;
        }
        
        if (c == '"') {
            inString = !inString;
            continue;
        }
        
        if (!inString) {
            if (c == '{') braceCount++;
            else if (c == '}') braceCount--;
            else if (c == '[') bracketCount++;
            else if (c == ']') bracketCount--;
        }
        
        if (braceCount < 0 || bracketCount < 0) {
            addIssue(result, Severity::Error, "",
                     "Unbalanced braces/brackets in JSON",
                     "E_UNBALANCED_JSON");
            return result;
        }
    }
    
    if (braceCount != 0 || bracketCount != 0) {
        addIssue(result, Severity::Error, "",
                 "Unclosed braces or brackets in JSON",
                 "E_UNCLOSED_JSON");
        return result;
    }
    
    // Run detailed validation
    validateVersion(jsonContent, result);
    validateStructure(jsonContent, result);
    validateTimeline(jsonContent, result);
    validateClips(jsonContent, result);
    validatePatterns(jsonContent, result);
    validateSources(jsonContent, result);
    validateMixer(jsonContent, result);
    
    if (m_config.validateIdUniqueness) {
        validateIdUniqueness(jsonContent, result);
    }
    
    if (m_config.validateAudioFiles || m_config.checkAssetPaths) {
        validateAssets(jsonContent, result);
    }
    
    // Run custom validator if provided
    if (m_config.customValidator) {
        auto customResult = m_config.customValidator(jsonContent);
        result.issues.insert(result.issues.end(), 
                            customResult.issues.begin(), 
                            customResult.issues.end());
    }
    
    return result;
}

bool ProjectValidator::isValidFile(const std::string& filePath) const {
    return validateFile(filePath).isValid();
}

//==============================================================================
// Schema Information
//==============================================================================

bool ProjectValidator::canLoadVersion(const SchemaVersion& version) const {
    return version >= m_config.minSupportedVersion;
}

bool ProjectValidator::needsMigration(const SchemaVersion& version) const {
    return version.major < m_config.supportedVersion.major;
}

//==============================================================================
// Internal Validation
//==============================================================================

void ProjectValidator::validateStructure(const std::string& json, ValidationResult& result) const {
    // Check for required top-level fields
    const char* requiredFields[] = {
        "\"version\"",
        "\"tempo\"",
        "\"sources\"",
        "\"patterns\"",
        "\"timeline\""
    };
    
    for (const auto* field : requiredFields) {
        if (json.find(field) == std::string::npos) {
            addIssue(result, Severity::Error, "root",
                     std::string("Missing required field: ") + field + "\"",
                     "E_MISSING_REQUIRED_FIELD");
        }
    }
}

void ProjectValidator::validateVersion(const std::string& json, ValidationResult& result) const {
    // Extract version field
    size_t versionPos = json.find("\"version\"");
    if (versionPos == std::string::npos) {
        addIssue(result, Severity::Error, "version",
                 "Version field not found",
                 "E_MISSING_VERSION");
        return;
    }
    
    // Look for version value after the key
    size_t colonPos = json.find(":", versionPos);
    if (colonPos == std::string::npos) {
        addIssue(result, Severity::Error, "version",
                 "Version field malformed",
                 "E_MALFORMED_VERSION");
        return;
    }
    
    // Extract version number (handle both string and number formats)
    size_t valueStart = json.find_first_not_of(" \t", colonPos + 1);
    if (valueStart == std::string::npos) {
        addIssue(result, Severity::Error, "version",
                 "Version value missing",
                 "E_MISSING_VERSION_VALUE");
        return;
    }
    
    // Try to parse version
    std::string versionStr;
    if (json[valueStart] == '"') {
        // String format: "1.0.0"
        size_t valueEnd = json.find("\"", valueStart + 1);
        if (valueEnd != std::string::npos) {
            versionStr = json.substr(valueStart + 1, valueEnd - valueStart - 1);
        }
    } else {
        // Number format: 1 or 1.0
        size_t valueEnd = json.find_first_of(",}\n", valueStart);
        if (valueEnd != std::string::npos) {
            versionStr = json.substr(valueStart, valueEnd - valueStart);
        }
    }
    
    auto version = SchemaVersion::fromString(versionStr);
    if (!version) {
        addIssue(result, Severity::Error, "version",
                 "Invalid version format: " + versionStr,
                 "E_INVALID_VERSION_FORMAT");
        return;
    }
    
    // Check if we can load this version
    if (!canLoadVersion(*version)) {
        addIssue(result, Severity::Error, "version",
                 "Project version " + version->toString() + 
                 " is not supported (minimum: " + 
                 m_config.minSupportedVersion.toString() + ")",
                 "E_UNSUPPORTED_VERSION");
    } else if (needsMigration(*version)) {
        addIssue(result, Severity::Warning, "version",
                 "Project version " + version->toString() + 
                 " requires migration to " + 
                 m_config.supportedVersion.toString(),
                 "W_NEEDS_MIGRATION");
    }
}

void ProjectValidator::validateTimeline(const std::string& json, ValidationResult& result) const {
    // Check timeline structure
    size_t timelinePos = json.find("\"timeline\"");
    if (timelinePos == std::string::npos) {
        addIssue(result, Severity::Warning, "timeline",
                 "Timeline field not found",
                 "W_MISSING_TIMELINE");
        return;
    }
    
    // Check for lanes array
    if (json.find("\"lanes\"", timelinePos) == std::string::npos &&
        json.find("\"clips\"", timelinePos) == std::string::npos) {
        addIssue(result, Severity::Warning, "timeline",
                 "Timeline has no lanes or clips",
                 "W_EMPTY_TIMELINE");
    }
}

void ProjectValidator::validateClips(const std::string& json, ValidationResult& result) const {
    // Basic clip validation - check for common issues
    size_t pos = 0;
    int clipIndex = 0;
    
    while ((pos = json.find("\"id\"", pos)) != std::string::npos) {
        // Look for clip-like structure near this ID
        size_t contextStart = pos > 200 ? pos - 200 : 0;
        std::string context = json.substr(contextStart, 400);
        
        // Check for required clip fields in context
        if (context.find("\"start\"") != std::string::npos ||
            context.find("\"startBeat\"") != std::string::npos) {
            
            // This looks like a clip, validate it
            if (context.find("\"patternId\"") == std::string::npos &&
                context.find("\"pattern\"") == std::string::npos) {
                addIssue(result, Severity::Warning, 
                         "timeline.clips[" + std::to_string(clipIndex) + "]",
                         "Clip may be missing pattern reference",
                         "W_CLIP_MISSING_PATTERN");
            }
            
            clipIndex++;
        }
        
        pos++;
    }
}

void ProjectValidator::validatePatterns(const std::string& json, ValidationResult& result) const {
    size_t patternsPos = json.find("\"patterns\"");
    if (patternsPos == std::string::npos) {
        addIssue(result, Severity::Warning, "patterns",
                 "No patterns array found",
                 "W_MISSING_PATTERNS");
        return;
    }
    
    // Check for pattern structure
    // Look for pattern ID field
    if (json.find("\"patternId\"", patternsPos) == std::string::npos &&
        json.find("\"id\"", patternsPos) == std::string::npos) {
        addIssue(result, Severity::Warning, "patterns",
                 "Patterns may be missing IDs",
                 "W_PATTERN_MISSING_ID");
    }
}

void ProjectValidator::validateSources(const std::string& json, ValidationResult& result) const {
    size_t sourcesPos = json.find("\"sources\"");
    if (sourcesPos == std::string::npos) {
        addIssue(result, Severity::Warning, "sources",
                 "No sources array found",
                 "W_MISSING_SOURCES");
        return;
    }
    
    // Extract source paths and validate them
    if (m_config.checkAssetPaths) {
        size_t pos = sourcesPos;
        int sourceIndex = 0;
        
        while ((pos = json.find("\"path\"", pos)) != std::string::npos) {
            // Extract path value
            size_t nextPos = pos + 1;
            size_t colonPos = json.find(":", pos);
            if (colonPos != std::string::npos) {
                size_t valueStart = json.find("\"", colonPos);
                if (valueStart != std::string::npos) {
                    size_t valueEnd = json.find("\"", valueStart + 1);
                    if (valueEnd != std::string::npos) {
                        std::string path = json.substr(valueStart + 1, valueEnd - valueStart - 1);
                        
                        if (!isValidPath(path)) {
                            addIssue(result, Severity::Warning,
                                     "sources[" + std::to_string(sourceIndex) + "].path",
                                     "Source path may be invalid: " + path,
                                     "W_INVALID_SOURCE_PATH");
                        }
                        
                        sourceIndex++;
                        nextPos = valueEnd + 1;
                    }
                }
            }
            pos = nextPos;
        }
    }
}

void ProjectValidator::validateMixer(const std::string& json, ValidationResult& result) const {
    // Mixer is optional, but if present validate it
    size_t mixerPos = json.find("\"mixer\"");
    if (mixerPos == std::string::npos) {
        // Mixer not present - that's fine
        return;
    }
    
    // Check for channels array
    if (json.find("\"channels\"", mixerPos) == std::string::npos) {
        addIssue(result, Severity::Info, "mixer",
                 "Mixer has no channels defined",
                 "I_MIXER_NO_CHANNELS");
    }
}

void ProjectValidator::validateAssets(const std::string& json, ValidationResult& result) const {
    if (!m_config.validateAudioFiles) return;
    
    // Find all path references and check if files exist
    size_t pos = 0;
    while ((pos = json.find("\"path\"", pos)) != std::string::npos) {
        size_t colonPos = json.find(":", pos);
        if (colonPos != std::string::npos) {
            size_t valueStart = json.find("\"", colonPos);
            if (valueStart != std::string::npos) {
                size_t valueEnd = json.find("\"", valueStart + 1);
                if (valueEnd != std::string::npos) {
                    std::string path = json.substr(valueStart + 1, valueEnd - valueStart - 1);
                    
                    // Only check audio file paths
                    if (path.find(".wav") != std::string::npos ||
                        path.find(".mp3") != std::string::npos ||
                        path.find(".flac") != std::string::npos) {
                        
                        if (!fileExists(path)) {
                            addIssue(result, Severity::Warning, "",
                                     "Referenced audio file not found: " + path,
                                     "W_MISSING_AUDIO_FILE");
                        }
                    }
                }
            }
        }
        pos++;
    }
}

void ProjectValidator::validateIdUniqueness(const std::string& json, ValidationResult& result) const {
    std::set<std::string> ids;
    size_t pos = 0;
    
    while ((pos = json.find("\"id\"", pos)) != std::string::npos) {
        // Extract ID value
        size_t colonPos = json.find(":", pos);
        if (colonPos != std::string::npos) {
            size_t valueStart = json.find_first_of("\"0123456789", colonPos);
            if (valueStart != std::string::npos) {
                std::string id;
                if (json[valueStart] == '"') {
                    size_t valueEnd = json.find("\"", valueStart + 1);
                    if (valueEnd != std::string::npos) {
                        id = json.substr(valueStart + 1, valueEnd - valueStart - 1);
                    }
                } else {
                    size_t valueEnd = json.find_first_of(",}\n", valueStart);
                    if (valueEnd != std::string::npos) {
                        id = json.substr(valueStart, valueEnd - valueStart);
                    }
                }
                
                if (!id.empty()) {
                    if (ids.count(id) > 0) {
                        addIssue(result, Severity::Error, "",
                                 "Duplicate ID found: " + id,
                                 "E_DUPLICATE_ID");
                    } else {
                        ids.insert(id);
                    }
                }
            }
        }
        pos++;
    }
}

//==============================================================================
// Helpers
//==============================================================================

void ProjectValidator::addIssue(ValidationResult& result, Severity severity, 
                                 const std::string& path, const std::string& message,
                                 const std::string& code) const {
    if (m_config.strictMode && severity == Severity::Warning) {
        severity = Severity::Error;
    }
    
    result.issues.push_back({severity, path, message, code});
}

bool ProjectValidator::fileExists(const std::string& path) const {
    try {
        return std::filesystem::exists(path);
    } catch (...) {
        return false;
    }
}

bool ProjectValidator::isValidPath(const std::string& path) const {
    // Basic path validation
    if (path.empty()) return false;
    
    // Check for invalid characters
    if (path.find('\0') != std::string::npos) return false;
    
    // Windows reserved characters (if on Windows)
    #ifdef _WIN32
    if (path.find_first_of("<>:\"|?*") != std::string::npos) return false;
    #endif
    
    return true;
}

bool ProjectValidator::isValidId(const std::string& id) const {
    if (id.empty()) return false;
    
    // IDs should be alphanumeric with underscores/dashes
    return std::all_of(id.begin(), id.end(), [](char c) {
        return std::isalnum(c) || c == '_' || c == '-';
    });
}

} // namespace Audio
} // namespace Aestra
