// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Project file validation and schema checking (v1.0)
 * 
 * Validates project files before loading to prevent crashes from corrupted
 * or malformed data. Performs both structural and semantic validation.
 * 
 * Usage:
 *   ProjectValidator validator;
 *   auto result = validator.validateFile("project.aes");
 *   if (!result.isValid()) {
 *       Log::error("Project invalid: " + result.getErrorSummary());
 *   }
 */
class ProjectValidator {
public:
    // =============================================================================
    // Types
    // =============================================================================
    
    enum class Severity {
        Info,       // Minor issue, can proceed
        Warning,    // Concerning but recoverable
        Error       // Critical, cannot load
    };
    
    struct Issue {
        Severity severity;
        std::string path;       // JSON path to issue (e.g., "timeline.clips[0].start")
        std::string message;    // Human-readable description
        std::string code;       // Machine-readable code (e.g., "E_MISSING_FIELD")
        
        bool isError() const { return severity == Severity::Error; }
    };
    
    struct ValidationResult {
        std::vector<Issue> issues;
        
        bool isValid() const {
            for (const auto& issue : issues) {
                if (issue.isError()) return false;
            }
            return true;
        }
        
        bool hasWarnings() const {
            for (const auto& issue : issues) {
                if (issue.severity == Severity::Warning) return true;
            }
            return false;
        }
        
        size_t errorCount() const {
            return std::count_if(issues.begin(), issues.end(),
                [](const Issue& i) { return i.isError(); });
        }
        
        size_t warningCount() const {
            return std::count_if(issues.begin(), issues.end(),
                [](const Issue& i) { return i.severity == Severity::Warning; });
        }
        
        std::string getErrorSummary() const {
            if (issues.empty()) return "No issues found";
            
            std::string summary;
            size_t errors = errorCount();
            size_t warnings = warningCount();
            
            if (errors > 0) {
                summary += std::to_string(errors) + " error" + (errors > 1 ? "s" : "");
            }
            if (warnings > 0) {
                if (!summary.empty()) summary += ", ";
                summary += std::to_string(warnings) + " warning" + (warnings > 1 ? "s" : "");
            }
            return summary;
        }
        
        std::vector<Issue> getErrors() const {
            std::vector<Issue> errors;
            for (const auto& issue : issues) {
                if (issue.isError()) errors.push_back(issue);
            }
            return errors;
        }
    };
    
    struct SchemaVersion {
        uint32_t major = 1;
        uint32_t minor = 0;
        uint32_t patch = 0;
        
        bool operator>=(const SchemaVersion& other) const {
            if (major != other.major) return major >= other.major;
            if (minor != other.minor) return minor >= other.minor;
            return patch >= other.patch;
        }
        
        std::string toString() const {
            return std::to_string(major) + "." + 
                   std::to_string(minor) + "." + 
                   std::to_string(patch);
        }
        
        static std::optional<SchemaVersion> fromString(const std::string& str);
    };
    
    // =============================================================================
    // Configuration
    // =============================================================================
    
    struct Config {
        // Schema version this validator supports
        SchemaVersion supportedVersion;
        SchemaVersion minSupportedVersion;
        
        // Validation strictness
        bool strictMode = false;           // Treat warnings as errors
        bool validateAudioFiles = true;    // Check referenced audio files exist
        bool checkAssetPaths = true;       // Validate asset path formats
        bool validateIdUniqueness = true;  // Check for duplicate IDs
        bool boundsCheckValues = true;     // Validate numeric ranges
        
        // Custom validation hook
        std::function<ValidationResult(const std::string& jsonContent)> customValidator;
        
        Config() 
            : supportedVersion{1, 0, 0}
            , minSupportedVersion{1, 0, 0}
        {}
    };
    
    // =============================================================================
    // Lifecycle
    // =============================================================================
    
    ProjectValidator(Config config = Config{});
    ~ProjectValidator() = default;
    
    // =============================================================================
    // Validation
    // =============================================================================
    
    /**
     * @brief Validate a project file from disk
     */
    ValidationResult validateFile(const std::string& filePath) const;
    
    /**
     * @brief Validate project JSON content directly
     */
    ValidationResult validateContent(const std::string& jsonContent) const;
    
    /**
     * @brief Quick check - just returns true/false without details
     */
    bool isValidFile(const std::string& filePath) const;
    
    // =============================================================================
    // Schema Information
    // =============================================================================
    
    /**
     * @brief Get the current schema version
     */
    SchemaVersion getSchemaVersion() const { return m_config.supportedVersion; }
    
    /**
     * @brief Check if a version can be loaded (migration available or same version)
     */
    bool canLoadVersion(const SchemaVersion& version) const;
    
    /**
     * @brief Check if migration is needed for this version
     */
    bool needsMigration(const SchemaVersion& version) const;
    
private:
    // =============================================================================
    // Internal Validation
    // =============================================================================
    
    void validateStructure(const std::string& json, ValidationResult& result) const;
    void validateVersion(const std::string& json, ValidationResult& result) const;
    void validateTimeline(const std::string& json, ValidationResult& result) const;
    void validateClips(const std::string& json, ValidationResult& result) const;
    void validatePatterns(const std::string& json, ValidationResult& result) const;
    void validateSources(const std::string& json, ValidationResult& result) const;
    void validateMixer(const std::string& json, ValidationResult& result) const;
    void validateAssets(const std::string& json, ValidationResult& result) const;
    void validateIdUniqueness(const std::string& json, ValidationResult& result) const;
    
    // Helper methods
    void addIssue(ValidationResult& result, Severity severity, 
                  const std::string& path, const std::string& message,
                  const std::string& code) const;
    
    bool fileExists(const std::string& path) const;
    bool isValidPath(const std::string& path) const;
    bool isValidId(const std::string& id) const;
    
private:
    Config m_config;
};

} // namespace Audio
} // namespace Aestra
