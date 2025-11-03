// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <sstream>

namespace Nomad {

// =============================================================================
// Lightweight JSON Parser
// =============================================================================
class JSON {
public:
    enum class Type {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object
    };

    JSON() : type_(Type::Null) {}
    JSON(bool value) : type_(Type::Boolean), boolValue_(value) {}
    JSON(double value) : type_(Type::Number), numberValue_(value) {}
    JSON(const std::string& value) : type_(Type::String), stringValue_(value) {}
    JSON(const char* value) : type_(Type::String), stringValue_(value) {}

    // Type checking
    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Boolean; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    // Value getters
    bool asBool() const { return boolValue_; }
    double asNumber() const { return numberValue_; }
    int asInt() const { return static_cast<int>(numberValue_); }
    const std::string& asString() const { return stringValue_; }

    // Array operations
    static JSON array() {
        JSON json;
        json.type_ = Type::Array;
        json.arrayValue_ = std::make_shared<std::vector<JSON>>();
        return json;
    }

    void push(const JSON& value) {
        if (type_ != Type::Array) return;
        arrayValue_->push_back(value);
    }

    size_t size() const {
        if (type_ == Type::Array) return arrayValue_->size();
        if (type_ == Type::Object) return objectValue_->size();
        return 0;
    }

    JSON& operator[](size_t index) {
        if (type_ != Type::Array || index >= arrayValue_->size()) {
            static JSON null;
            return null;
        }
        return (*arrayValue_)[index];
    }

    const JSON& operator[](size_t index) const {
        if (type_ != Type::Array || index >= arrayValue_->size()) {
            static JSON null;
            return null;
        }
        return (*arrayValue_)[index];
    }

    // Object operations
    static JSON object() {
        JSON json;
        json.type_ = Type::Object;
        json.objectValue_ = std::make_shared<std::map<std::string, JSON>>();
        return json;
    }

    void set(const std::string& key, const JSON& value) {
        if (type_ != Type::Object) return;
        (*objectValue_)[key] = value;
    }

    JSON& operator[](const std::string& key) {
        if (type_ != Type::Object) {
            static JSON null;
            return null;
        }
        return (*objectValue_)[key];
    }

    const JSON& operator[](const std::string& key) const {
        if (type_ != Type::Object) {
            static JSON null;
            return null;
        }
        auto it = objectValue_->find(key);
        if (it == objectValue_->end()) {
            static JSON null;
            return null;
        }
        return it->second;
    }

    bool has(const std::string& key) const {
        if (type_ != Type::Object) return false;
        return objectValue_->find(key) != objectValue_->end();
    }

    // Serialization
    std::string toString(int indent = 0) const {
        std::stringstream ss;
        serialize(ss, indent, 0);
        return ss.str();
    }

    // Parsing
    static JSON parse(const std::string& jsonString) {
        size_t pos = 0;
        return parseValue(jsonString, pos);
    }

private:
    Type type_;
    bool boolValue_ = false;
    double numberValue_ = 0.0;
    std::string stringValue_;
    std::shared_ptr<std::vector<JSON>> arrayValue_;
    std::shared_ptr<std::map<std::string, JSON>> objectValue_;

    void serialize(std::stringstream& ss, int indent, int depth) const {
        std::string indentStr(depth * indent, ' ');
        std::string nextIndentStr((depth + 1) * indent, ' ');

        switch (type_) {
            case Type::Null:
                ss << "null";
                break;
            case Type::Boolean:
                ss << (boolValue_ ? "true" : "false");
                break;
            case Type::Number:
                ss << numberValue_;
                break;
            case Type::String:
                ss << "\"" << stringValue_ << "\"";
                break;
            case Type::Array:
                ss << "[";
                if (indent > 0 && !arrayValue_->empty()) ss << "\n";
                for (size_t i = 0; i < arrayValue_->size(); ++i) {
                    if (indent > 0) ss << nextIndentStr;
                    (*arrayValue_)[i].serialize(ss, indent, depth + 1);
                    if (i < arrayValue_->size() - 1) ss << ",";
                    if (indent > 0) ss << "\n";
                }
                if (indent > 0 && !arrayValue_->empty()) ss << indentStr;
                ss << "]";
                break;
            case Type::Object:
                ss << "{";
                if (indent > 0 && !objectValue_->empty()) ss << "\n";
                size_t count = 0;
                for (const auto& pair : *objectValue_) {
                    if (indent > 0) ss << nextIndentStr;
                    ss << "\"" << pair.first << "\":";
                    if (indent > 0) ss << " ";
                    pair.second.serialize(ss, indent, depth + 1);
                    if (count < objectValue_->size() - 1) ss << ",";
                    if (indent > 0) ss << "\n";
                    count++;
                }
                if (indent > 0 && !objectValue_->empty()) ss << indentStr;
                ss << "}";
                break;
        }
    }

    static void skipWhitespace(const std::string& str, size_t& pos) {
        while (pos < str.size() && std::isspace(str[pos])) {
            pos++;
        }
    }

    static JSON parseValue(const std::string& str, size_t& pos) {
        skipWhitespace(str, pos);
        if (pos >= str.size()) return JSON();

        char c = str[pos];
        if (c == '{') return parseObject(str, pos);
        if (c == '[') return parseArray(str, pos);
        if (c == '"') return parseString(str, pos);
        if (c == 't' || c == 'f') return parseBool(str, pos);
        if (c == 'n') return parseNull(str, pos);
        if (c == '-' || std::isdigit(c)) return parseNumber(str, pos);

        return JSON();
    }

    static JSON parseObject(const std::string& str, size_t& pos) {
        JSON obj = JSON::object();
        pos++; // Skip '{'

        skipWhitespace(str, pos);
        if (pos < str.size() && str[pos] == '}') {
            pos++;
            return obj;
        }

        while (pos < str.size()) {
            skipWhitespace(str, pos);
            if (str[pos] != '"') break;

            JSON key = parseString(str, pos);
            skipWhitespace(str, pos);
            if (pos >= str.size() || str[pos] != ':') break;
            pos++; // Skip ':'

            JSON value = parseValue(str, pos);
            obj.set(key.asString(), value);

            skipWhitespace(str, pos);
            if (pos >= str.size()) break;
            if (str[pos] == '}') {
                pos++;
                break;
            }
            if (str[pos] == ',') {
                pos++;
            }
        }

        return obj;
    }

    static JSON parseArray(const std::string& str, size_t& pos) {
        JSON arr = JSON::array();
        pos++; // Skip '['

        skipWhitespace(str, pos);
        if (pos < str.size() && str[pos] == ']') {
            pos++;
            return arr;
        }

        while (pos < str.size()) {
            JSON value = parseValue(str, pos);
            arr.push(value);

            skipWhitespace(str, pos);
            if (pos >= str.size()) break;
            if (str[pos] == ']') {
                pos++;
                break;
            }
            if (str[pos] == ',') {
                pos++;
            }
        }

        return arr;
    }

    static JSON parseString(const std::string& str, size_t& pos) {
        pos++; // Skip opening '"'
        std::string value;
        while (pos < str.size() && str[pos] != '"') {
            if (str[pos] == '\\' && pos + 1 < str.size()) {
                pos++;
                switch (str[pos]) {
                    case 'n': value += '\n'; break;
                    case 't': value += '\t'; break;
                    case 'r': value += '\r'; break;
                    case '"': value += '"'; break;
                    case '\\': value += '\\'; break;
                    default: value += str[pos]; break;
                }
            } else {
                value += str[pos];
            }
            pos++;
        }
        if (pos < str.size()) pos++; // Skip closing '"'
        return JSON(value);
    }

    static JSON parseNumber(const std::string& str, size_t& pos) {
        size_t start = pos;
        if (str[pos] == '-') pos++;
        while (pos < str.size() && (std::isdigit(str[pos]) || str[pos] == '.')) {
            pos++;
        }
        double value = std::stod(str.substr(start, pos - start));
        return JSON(value);
    }

    static JSON parseBool(const std::string& str, size_t& pos) {
        if (str.substr(pos, 4) == "true") {
            pos += 4;
            return JSON(true);
        }
        if (str.substr(pos, 5) == "false") {
            pos += 5;
            return JSON(false);
        }
        return JSON();
    }

    static JSON parseNull(const std::string& str, size_t& pos) {
        if (str.substr(pos, 4) == "null") {
            pos += 4;
        }
        return JSON();
    }
};

} // namespace Nomad
