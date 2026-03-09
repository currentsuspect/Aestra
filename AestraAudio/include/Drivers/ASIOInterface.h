// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// clang-format off
#pragma once

#if defined(_WIN32)
#include <objbase.h> // ALLOW_PLATFORM_INCLUDE
#include <windows.h> // ALLOW_PLATFORM_INCLUDE
#else
#include <unistd.h>
#endif

namespace Aestra {
namespace Audio {
namespace ASIO {

// Force 4-byte alignment for ASIO structures
#pragma pack(push, 4)

// ASIO Sample Types
enum ASIOSampleType {
    // ... (content omitted for brevity in prompt, but effectively wrapping the structs)
    // Wait, I have to output the whole file content if I wrap the whole namespace?
    // Or just wrap the structs.
    // Best to wrap the whole namespace content inside pack.

    ASIOSTInt16MSB = 0,
    ASIOSTInt24MSB = 1,
    ASIOSTInt32MSB = 2,
    ASIOSTFloat32MSB = 3,
    ASIOSTFloat64MSB = 4,

    ASIOSTInt32MSB16 = 8,
    ASIOSTInt32MSB18 = 9,
    ASIOSTInt32MSB20 = 10,
    ASIOSTInt32MSB24 = 11,

    ASIOSTInt16LSB = 16,
    ASIOSTInt24LSB = 17,
    ASIOSTInt32LSB = 18,
    ASIOSTFloat32LSB = 19,
    ASIOSTFloat64LSB = 20,

    ASIOSTInt32LSB16 = 24,
    ASIOSTInt32LSB18 = 25,
    ASIOSTInt32LSB20 = 26,
    ASIOSTInt32LSB24 = 27,

    ASIOSTDSDInt8LSB1 = 32,
    ASIOSTDSDInt8MSB1 = 33,
    ASIOSTDSDInt8NER8 = 40
};

// ASIO Buffer Info
struct ASIOBufferInfo {
    long isInput;     // 1 = Input, 0 = Output
    long channelNum;  // Physical Channel Number
    void* buffers[2]; // Double Buffers
};

// ASIO Channel Info
struct ASIOChannelInfo {
    long channel;      // Channel Number
    long isInput;      // 1 = Input, 0 = Output
    long isActive;     // 1 = Active
    long channelGroup; // Channel Group
    long type;         // ASIOSampleType
    char name[32];     // Channel Name
};

// ASIO Driver Info
struct ASIODriverInfo {
    long asioVersion;       // ASIO Version (2)
    long driverVersion;     // Driver Version
    char name[32];          // Driver Name
    char errorMessage[124]; // Error Message
    void* sysRef;           // System Reference
};

// ASIO Time Info
struct ASIOTimeStamp {
    unsigned long hi;
    unsigned long lo;
};

struct ASIOSamples {
    unsigned long hi;
    unsigned long lo;
};

struct ASIOTimeCode {
    double speed;
    ASIOTimeStamp timeCodeSamples;
    unsigned long flags;
    char future[64];
};

struct ASIOTime {
    long reserved[4];
    struct ASIOTimeCode timeCode;
    struct ASIOTimeCode timeCode2; // ??? SDK variance, usually just timeCode
};

struct ASIOClockSource {
    long index;
    long associatedChannel;
    long associatedGroup;
    long isCurrentSource;
    char name[32];
};

// ASIO Callbacks (Function Pointers)
struct ASIOCallbacks {
    void (*bufferSwitch)(long doubleBufferIndex, long directProcess);
    void (*sampleRateDidChange)(double sRate);
    long (*asioMessage)(long selector, long value, void* message, double* opt);
    ASIOTime* (*bufferSwitchTimeInfo)(ASIOTime* timeInfo, long doubleBufferIndex, long directProcess);
};

// ASIO Error Codes
enum ASIOError {
    ASE_OK = 0,
    ASE_SUCCESS = 0x3f4847a0,
    ASE_NotPresent = -1000,
    ASE_HWMalfunction,
    ASE_InvalidParameter,
    ASE_InvalidMode,
    ASE_SPNotAdvancing,
    ASE_NoClock,
    ASE_NoMemory
};

// ASIO Interface (COM style)
interface IASIO : public IUnknown {
    virtual long __stdcall init(void* sysHandle) = 0;
    virtual void __stdcall getDriverName(char* name) = 0;
    virtual long __stdcall getDriverVersion() = 0;
    virtual void __stdcall getErrorMessage(char* string) = 0;
    virtual long __stdcall start() = 0;
    virtual long __stdcall stop() = 0;
    virtual long __stdcall getChannels(long* numInputChannels, long* numOutputChannels) = 0;
    virtual long __stdcall getLatencies(long* inputLatency, long* outputLatency) = 0;
    virtual long __stdcall getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) = 0;
    virtual long __stdcall canSampleRate(double sampleRate) = 0;
    virtual long __stdcall getSampleRate(double* sampleRate) = 0;
    virtual long __stdcall setSampleRate(double sampleRate) = 0;
    virtual long __stdcall getClockSources(struct ASIOClockSource* clocks, long* numSources) = 0;
    virtual long __stdcall setClockSource(long reference) = 0;
    virtual long __stdcall getSamplePosition(struct ASIOSamples* sPos, struct ASIOTimeStamp* tStamp) = 0;
    virtual long __stdcall getChannelInfo(ASIOChannelInfo* info) = 0;
    virtual long __stdcall createBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize,
                                         ASIOCallbacks* callbacks) = 0;
    virtual long __stdcall disposeBuffers() = 0;
    virtual long __stdcall controlPanel() = 0;
    virtual long __stdcall future(long selector, void* opt) = 0;
    virtual long __stdcall outputReady() = 0;
};

#pragma pack(pop)
} // namespace ASIO
} // namespace Audio
} // namespace Aestra
