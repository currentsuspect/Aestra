// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "../include/NomadFile.h"
#include "../include/NomadJSON.h"
#include <iostream>
#include <cassert>

using namespace Nomad;

#define TEST_ASSERT(condition, message) \
    if (!(condition)) { \
        std::cerr << "FAILED: " << message << std::endl; \
        return false; \
    }

// =============================================================================
// File Tests
// =============================================================================
bool testFile() {
    std::cout << "Testing File..." << std::endl;

    const std::string testPath = "test_file.txt";
    const std::string testContent = "Hello, NOMAD!";

    // Test write
    TEST_ASSERT(File::writeAllText(testPath, testContent), "Should write file");
    TEST_ASSERT(File::exists(testPath), "File should exist");

    // Test read
    std::string readContent = File::readAllText(testPath);
    TEST_ASSERT(readContent == testContent, "Should read correct content");

    // Test File class
    File file;
    TEST_ASSERT(file.open(testPath, File::Mode::Read), "Should open file");
    TEST_ASSERT(file.isOpen(), "File should be open");
    
    size_t fileSize = file.size();
    TEST_ASSERT(fileSize == testContent.size(), "File size should match");

    std::vector<char> buffer(fileSize);
    TEST_ASSERT(file.read(buffer.data(), fileSize), "Should read data");
    file.close();
    TEST_ASSERT(!file.isOpen(), "File should be closed");

    // Cleanup
    std::remove(testPath.c_str());

    std::cout << "  âœ“ File tests passed" << std::endl;
    return true;
}

// =============================================================================
// Binary Serialization Tests
// =============================================================================
bool testBinarySerialization() {
    std::cout << "Testing Binary Serialization..." << std::endl;

    // Test writing
    BinaryWriter writer;
    writer.write(static_cast<int8_t>(-42));
    writer.write(static_cast<uint8_t>(255));
    writer.write(static_cast<int16_t>(-1000));
    writer.write(static_cast<uint16_t>(60000));
    writer.write(static_cast<int32_t>(-100000));
    writer.write(static_cast<uint32_t>(4000000000));
    writer.write(3.14159f);
    writer.write(2.71828);
    writer.write(std::string("NOMAD"));

    // Test reading
    BinaryReader reader(writer.data());
    
    int8_t i8;
    TEST_ASSERT(reader.read(i8) && i8 == -42, "Should read int8");
    
    uint8_t u8;
    TEST_ASSERT(reader.read(u8) && u8 == 255, "Should read uint8");
    
    int16_t i16;
    TEST_ASSERT(reader.read(i16) && i16 == -1000, "Should read int16");
    
    uint16_t u16;
    TEST_ASSERT(reader.read(u16) && u16 == 60000, "Should read uint16");
    
    int32_t i32;
    TEST_ASSERT(reader.read(i32) && i32 == -100000, "Should read int32");
    
    uint32_t u32;
    TEST_ASSERT(reader.read(u32) && u32 == 4000000000, "Should read uint32");
    
    float f;
    TEST_ASSERT(reader.read(f) && std::abs(f - 3.14159f) < 0.0001f, "Should read float");
    
    double d;
    TEST_ASSERT(reader.read(d) && std::abs(d - 2.71828) < 0.0001, "Should read double");
    
    std::string str;
    TEST_ASSERT(reader.read(str) && str == "NOMAD", "Should read string");

    // Test file I/O
    const std::string testPath = "test_binary.bin";
    TEST_ASSERT(writer.writeToFile(testPath), "Should write binary file");
    
    auto fileReader = BinaryReader::readFromFile(testPath);
    TEST_ASSERT(fileReader != nullptr, "Should read binary file");
    
    TEST_ASSERT(fileReader->read(i8) && i8 == -42, "Should read int8 from file");

    // Cleanup
    std::remove(testPath.c_str());

    std::cout << "  âœ“ Binary Serialization tests passed" << std::endl;
    return true;
}

// =============================================================================
// JSON Tests
// =============================================================================
bool testJSON() {
    std::cout << "Testing JSON..." << std::endl;

    // Test primitives
    JSON nullValue;
    TEST_ASSERT(nullValue.isNull(), "Should be null");

    JSON boolValue(true);
    TEST_ASSERT(boolValue.isBool() && boolValue.asBool(), "Should be true");

    JSON numberValue(42.5);
    TEST_ASSERT(numberValue.isNumber() && numberValue.asNumber() == 42.5, "Should be 42.5");
    TEST_ASSERT(numberValue.asInt() == 42, "Should convert to int");

    JSON stringValue("Hello");
    TEST_ASSERT(stringValue.isString() && stringValue.asString() == "Hello", "Should be 'Hello'");

    // Test array
    JSON arr = JSON::array();
    arr.push(JSON(1.0));
    arr.push(JSON(2.0));
    arr.push(JSON(3.0));
    TEST_ASSERT(arr.isArray() && arr.size() == 3, "Array should have 3 elements");
    TEST_ASSERT(arr[0].asNumber() == 1.0, "First element should be 1");
    TEST_ASSERT(arr[1].asNumber() == 2.0, "Second element should be 2");
    TEST_ASSERT(arr[2].asNumber() == 3.0, "Third element should be 3");

    // Test object
    JSON obj = JSON::object();
    obj.set("name", JSON("NOMAD"));
    obj.set("version", JSON(1.0));
    obj.set("active", JSON(true));
    TEST_ASSERT(obj.isObject() && obj.size() == 3, "Object should have 3 properties");
    TEST_ASSERT(obj.has("name"), "Should have 'name' property");
    TEST_ASSERT(obj["name"].asString() == "NOMAD", "Name should be 'NOMAD'");
    TEST_ASSERT(obj["version"].asNumber() == 1.0, "Version should be 1.0");
    TEST_ASSERT(obj["active"].asBool(), "Active should be true");

    // Test serialization
    std::string jsonStr = obj.toString();
    TEST_ASSERT(!jsonStr.empty(), "Should serialize to string");

    // Test parsing
    std::string testJson = R"({"name":"NOMAD","version":1.0,"active":true,"tags":["audio","daw"]})";
    JSON parsed = JSON::parse(testJson);
    TEST_ASSERT(parsed.isObject(), "Should parse object");
    TEST_ASSERT(parsed["name"].asString() == "NOMAD", "Should parse name");
    TEST_ASSERT(parsed["version"].asNumber() == 1.0, "Should parse version");
    TEST_ASSERT(parsed["active"].asBool(), "Should parse active");
    TEST_ASSERT(parsed["tags"].isArray(), "Should parse array");
    TEST_ASSERT(parsed["tags"].size() == 2, "Array should have 2 elements");
    TEST_ASSERT(parsed["tags"][0].asString() == "audio", "First tag should be 'audio'");

    // Test nested objects
    JSON nested = JSON::object();
    JSON settings = JSON::object();
    settings.set("sampleRate", JSON(48000.0));
    settings.set("bufferSize", JSON(512.0));
    nested.set("audio", settings);
    TEST_ASSERT(nested["audio"]["sampleRate"].asNumber() == 48000.0, "Should access nested property");

    std::cout << "  âœ“ JSON tests passed" << std::endl;
    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================
int main() {
    std::cout << "\n==================================" << std::endl;
    std::cout << "  NomadCore File I/O Tests" << std::endl;
    std::cout << "==================================" << std::endl;

    bool allPassed = true;
    allPassed &= testFile();
    allPassed &= testBinarySerialization();
    allPassed &= testJSON();

    std::cout << "\n==================================" << std::endl;
    if (allPassed) {
        std::cout << "  âœ“ ALL TESTS PASSED" << std::endl;
    } else {
        std::cout << "  âœ— SOME TESTS FAILED" << std::endl;
    }
    std::cout << "==================================" << std::endl;

    return allPassed ? 0 : 1;
}
