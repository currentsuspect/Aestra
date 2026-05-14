#include "Commands/SessionLog.h"

#include <chrono>
#include <sstream>

namespace Aestra {
namespace Audio {

SessionLog::SessionLog(const std::string& path) {
    m_file.open(path, std::ios::app);
}

SessionLog::~SessionLog() {
    if (m_file.is_open())
        m_file.close();
}

void SessionLog::append(
    const CommandResult& result,
    const std::unordered_map<std::string, std::string>& resolvedArgs,
    const std::string& rawInput)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_file.is_open())
        return;

    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    const char* statusStr = "unknown";
    switch (result.status) {
    case CommandStatus::Success: statusStr = "success"; break;
    case CommandStatus::ParseError: statusStr = "parse_error"; break;
    case CommandStatus::ValidationError: statusStr = "validation_error"; break;
    case CommandStatus::ExecutionError: statusStr = "execution_error"; break;
    }

    std::ostringstream json;
    json << "{\n";
    json << "  \"v\": 1,\n";
    json << "  \"id\": " << result.commandId << ",\n";
    json << "  \"ts\": " << ms << ",\n";
    json << "  \"cmd\": \"" << result.verb << "\",\n";
    json << "  \"args\": {";
    bool first = true;
    for (const auto& kv : resolvedArgs) {
        if (!first) json << ", ";
        first = false;
        json << "\"" << kv.first << "\": \"" << kv.second << "\"";
    }
    json << "},\n";
    json << "  \"status\": \"" << statusStr << "\",\n";
    json << "  \"raw\": \"" << rawInput << "\",\n";
    json << "  \"undoable\": " << (result.undoable ? "true" : "false") << ",\n";
    json << "  \"exec_ms\": " << result.executionMs << "\n";
    json << "}\n";

    m_file << json.str();
    m_file.flush();
}

} // namespace Audio
} // namespace Aestra
