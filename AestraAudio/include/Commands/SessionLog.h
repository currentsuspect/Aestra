#pragma once

#include "Commands/CommandResult.h"

#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Aestra {
namespace Audio {

class SessionLog {
public:
    explicit SessionLog(const std::string& path);
    ~SessionLog();

    SessionLog(const SessionLog&) = delete;
    SessionLog& operator=(const SessionLog&) = delete;

    void append(const CommandResult& result,
                const std::unordered_map<std::string, std::string>& resolvedArgs,
                const std::string& rawInput);

private:
    std::ofstream m_file;
    std::mutex m_mutex;
};

} // namespace Audio
} // namespace Aestra
