// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Undo, redo and batch — the verbs that move CommandHistory.
//

//
// Part of the split that took MuseService.cpp from one 2200-line dispatch
// chain to one file per family. See Commands/MuseServiceInternal.h for why, and
// for the contract these handlers share.
//
// The body below is the text it was before the move, dedented by four spaces
// and otherwise untouched. If you change a verb's behaviour, change it here.

#include "Commands/MuseServiceInternal.h"
#include "Core/AudioEngine.h"
#include "Commands/CommandParser.h"
#include "Commands/CommandTransaction.h"
#include "Models/TrackManager.h"

#include "AestraJSON.h"
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Aestra {
namespace Audio {

// Inside Aestra::Audio, as it was in MuseService.cpp: JSON is Aestra::JSON, so
// these helpers cannot sit at global scope and still name it unqualified.

namespace MuseInternal {

std::optional<std::string> handleHistoryVerbs(const RequestContext& ctx, const ResponseEnvelope& env) {
    // Bound to the names the moved bodies already use, so no verb body had to be
    // edited to make the move.
    const double id = ctx.id;
    const std::string& verb = ctx.verb;
    JSON& request = *ctx.request;
    TrackManager* const m_trackManager = ctx.trackManager;
    AudioEngine* const m_engine = ctx.engine;
    // A reference, not a pointer: get_project_load_report tests this for a value
    // and then dereferences it, so it has to carry std::optional's own semantics.
    // A pointer would be truthy whenever the address is non-null — including when
    // the optional it points at is empty — which is not the same question.
    const std::optional<JSON>& m_projectLoadReport = *ctx.projectLoadReport;

    const auto makeOk = [&env]() { return env.ok(); };
    const auto finish = [&env](JSON& response) { return env.finish(response); };

    // ------------------------------------------------------------------
    // undo / redo — drive the same history the UI's Ctrl+Z drives.
    //
    // Every mutation verb and every batch already lands there as one step;
    // without these the surface could build that history but never walk it,
    // so the only way back from a mistake was to hand-write the inverse
    // edit. Against the live app these move the user's undo stack, which is
    // the point: agent edits and hand edits are the same edits.
    // ------------------------------------------------------------------
    if (verb == "undo" || verb == "redo") {
        if (!m_trackManager) {
            return makeError(id, "execution_error", "no track manager", verb).toString();
        }
        if (request.has("args") && request["args"].isObject() &&
            !request["args"].asObject().empty()) {
            return makeError(id, "validation_error", verb + " takes no args", verb).toString();
        }

        CommandHistory& history = m_trackManager->getCommandHistory();
        const bool undoing = (verb == "undo");

        // An empty history is a refusal, not a silent success: a caller that
        // gets ok back would reasonably believe an edit was reverted.
        if (undoing ? !history.canUndo() : !history.canRedo()) {
            return makeError(id, "execution_error",
                             undoing ? "nothing to undo" : "nothing to redo", verb)
                .toString();
        }
        if (!(undoing ? history.undo() : history.redo())) {
            return makeError(id, "execution_error",
                             std::string(undoing ? "undo" : "redo") + " failed", verb)
                .toString();
        }

        // Report the new ends of the stack so a caller can walk it without
        // guessing when to stop.
        JSON result = JSON::object();
        result.set("canUndo", JSON(history.canUndo()));
        result.set("canRedo", JSON(history.canRedo()));
        JSON response = makeOk();
        response.set("result", result);
        return finish(response);
    }

    if (verb == "batch") {
        if (!m_trackManager) {
            return makeError(id, "execution_error", "no track manager", verb).toString();
        }
        if (!request.has("args") || !request["args"].isObject()) {
            return makeError(id, "validation_error",
                             "batch requires args: {\"commands\": [{\"verb\": ..., \"args\": "
                             "...}, ...]}",
                             verb)
                .toString();
        }
        JSON& args = request["args"];
        for (auto& entry : args.asObject()) {
            if (entry.first != "commands") {
                return makeError(id, "validation_error",
                                 "unknown arg for batch: " + entry.first, verb)
                    .toString();
            }
        }
        if (!args.has("commands") || !args["commands"].isArray()) {
            return makeError(id, "validation_error", "arg 'commands' must be an array", verb)
                .toString();
        }
        JSON& commands = args["commands"];
        const size_t count = commands.size();
        constexpr size_t kMaxBatchCommands = 64;
        if (count == 0) {
            return makeError(id, "validation_error", "batch must contain at least one command",
                             verb)
                .toString();
        }
        if (count > kMaxBatchCommands) {
            return makeError(id, "validation_error",
                             "batch too large: " + std::to_string(count) + " commands (max " +
                                 std::to_string(kMaxBatchCommands) + ")",
                             verb)
                .toString();
        }

        // All-or-nothing, stepwise: each member is validated, built, and
        // executed against the state its predecessors produced — so a
        // batch can set the pan of a track it just added. On any failure
        // the executed prefix is undone in reverse and nothing is
        // recorded. On success the whole group lands in history as one
        // already-executed CommandTransaction: a single undo step.
        CommandParser parser;
        auto transaction = std::make_shared<CommandTransaction>("Muse Batch");
        std::vector<std::shared_ptr<ICommand>> executed;
        executed.reserve(count);
        // Unwind the executed prefix in reverse and produce the final
        // error response. A rollback failure must not hide behind the
        // member error: the response then reports execution_error and
        // says the session may be inconsistent.
        const auto failBatch = [&](const char* errorStatus,
                                   const std::string& message) -> std::string {
            bool rollbackClean = true;
            for (auto it = executed.rbegin(); it != executed.rend(); ++it) {
                try {
                    (*it)->undo();
                } catch (...) {
                    // Keep unwinding the rest of the prefix, but remember.
                    rollbackClean = false;
                }
            }
            if (!rollbackClean) {
                return makeError(id, "execution_error",
                                 message +
                                     " (rollback also failed; session state may be "
                                     "inconsistent)",
                                 verb)
                    .toString();
            }
            return makeError(id, errorStatus, message, verb).toString();
        };

        for (size_t i = 0; i < count; ++i) {
            const std::string prefix = "commands[" + std::to_string(i) + "]: ";
            JSON& item = commands[i];
            if (!item.isObject() || !item.has("verb") || !item["verb"].isString()) {
                return failBatch("validation_error",
                                 prefix + "must be an object with a string verb");
            }
            // Refuse unknown member keys by name, the way every other args
            // object here does. Accepting them silently is how a member
            // written with "flags" instead of "args" runs with NO args and
            // still reports ok — an add_track that quietly takes its default
            // name rather than the one asked for.
            for (auto& memberEntry : item.asObject()) {
                if (memberEntry.first != "verb" && memberEntry.first != "args") {
                    return failBatch("validation_error",
                                     prefix + "unknown key: " + memberEntry.first +
                                         " (a member is {\"verb\": ..., \"args\": ...})");
                }
            }
            const std::string subVerb = item["verb"].asString();
            if (isQueryVerb(subVerb) || isActionVerb(subVerb)) {
                return failBatch("validation_error",
                                 prefix + "only mutation verbs are allowed in a batch");
            }

            std::unordered_map<std::string, std::string> flags;
            if (item.has("args")) {
                if (!item["args"].isObject()) {
                    return failBatch("validation_error", prefix + "args must be an object");
                }
                std::string convertError;
                if (!jsonArgsToFlags(item["args"], flags, convertError)) {
                    return failBatch("validation_error", prefix + convertError);
                }
            }

            CommandStatus buildStatus = CommandStatus::Success;
            std::string buildMessage;
            const CommandContext ctx{m_engine, m_trackManager};
            auto built = parser.buildValidated(subVerb, flags, buildStatus, buildMessage, ctx);
            if (!built) {
                return failBatch(statusName(buildStatus), prefix + buildMessage);
            }

            std::shared_ptr<ICommand> cmd(std::move(built));
            try {
                cmd->execute();
            } catch (const std::exception& e) {
                return failBatch("execution_error", prefix + e.what());
            } catch (...) {
                return failBatch("execution_error", prefix + "command threw");
            }
            executed.push_back(cmd);
            transaction->add(cmd);
        }

        transaction->markExecuted();
        if (!m_trackManager->getCommandHistory().pushExecuted(transaction)) {
            // Executed but not recorded (e.g. a deferred transaction is
            // active on this history). "ok, undoable" would be a lie —
            // keep all-or-nothing honest by rolling the batch back.
            return failBatch("execution_error",
                             "batch could not be recorded in history; rolled back");
        }

        JSON result = JSON::object();
        result.set("count", JSON(static_cast<double>(count)));
        JSON response = makeOk();
        response.set("result", result);
        response.set("undoable", JSON(true));
        return finish(response);
    }
    // No verb matched. In the single-file version this fell out of the if-chain
    // into the next family's checks and eventually the mutation path, which is
    // exactly what nullopt asks handleRequest to do.
    return std::nullopt;
}

} // namespace MuseInternal
} // namespace Audio
} // namespace Aestra
