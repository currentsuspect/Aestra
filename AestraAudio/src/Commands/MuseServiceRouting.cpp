// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Routing and health diagnostics: audio health, the routing
//
//  * graph, latency compensation.
//
// Part of the split that took MuseService.cpp from one 2200-line dispatch
// chain to one file per family. See Commands/MuseServiceInternal.h for why, and
// for the contract these handlers share.
//
// The body below is the text it was before the move, dedented by four spaces
// and otherwise untouched. If you change a verb's behaviour, change it here.

#include "Commands/MuseServiceInternal.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"
#include "Models/MeterSnapshot.h"
#include "Models/UnitManager.h"

#include "AestraJSON.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Aestra {
namespace Audio {

// Inside Aestra::Audio, as it was in MuseService.cpp: JSON is Aestra::JSON, so
// these helpers cannot sit at global scope and still name it unqualified.

namespace MuseInternal {

std::optional<std::string> handleRoutingVerbs(const RequestContext& ctx, const ResponseEnvelope& env) {
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

    if (verb == "get_audio_health") {
        if (!m_engine) {
            return makeError(id, "execution_error", "no audio engine", verb).toString();
        }

        // Consume only lock-free state the callback already publishes. A
        // diagnostic read must never make the realtime thread wait.
        const auto& telemetry = m_engine->telemetry();
        const uint64_t blocks = telemetry.getBlocksProcessed();
        const uint64_t timedCallbacks = telemetry.getTimedCallbackCount();
        const uint64_t xruns = telemetry.getXruns();
        const uint64_t underruns = telemetry.getUnderruns();
        const uint64_t overruns = telemetry.getOverruns();
        const uint64_t queueDrops = m_engine->commandQueue().droppedCount();
        const uint64_t queueEdgeDrops = m_engine->commandQueue().edgeDroppedCount();
        const uint64_t rtMisuse = telemetry.getRtMisuseViolations();
        const uint64_t nanSamples = m_engine->getNaNCount();
        const uint64_t clippedSamples = m_engine->getClipCount();
        const bool recoveryActive = telemetry.isInRecoveryMode();
        const int32_t linuxPriorityErrno = telemetry.getLinuxRtPriorityErrno();

        JSON issues = JSON::array();
        const auto addIssue = [&](bool present, const char* code) {
            if (present) issues.push(JSON(code));
        };
        addIssue(xruns > 0, "xruns");
        addIssue(underruns > 0, "underruns");
        addIssue(overruns > 0, "callback_deadline_overruns");
        addIssue(queueDrops > 0, "command_queue_drops");
        addIssue(queueEdgeDrops > 0, "command_queue_edge_drops");
        addIssue(rtMisuse > 0, "rt_misuse_violations");
        addIssue(nanSamples > 0, "nan_samples_sanitized");
        addIssue(clippedSamples > 0, "hard_clipped_samples");
        addIssue(recoveryActive, "underrun_recovery_active");
        addIssue(timedCallbacks > 0 && !telemetry.isThreadPriorityOptimal(),
                 "realtime_priority_incomplete");
        addIssue(linuxPriorityErrno != 0, "realtime_priority_error");

        const bool observed = blocks > 0 || timedCallbacks > 0;
        const bool degraded = issues.size() > 0;

        JSON timing = JSON::object();
        timing.set("blocksProcessed", JSON(static_cast<double>(blocks)));
        timing.set("timedCallbacks", JSON(static_cast<double>(timedCallbacks)));
        timing.set("lastCallbackMs",
                   JSON(static_cast<double>(telemetry.getLastCallbackNs()) / 1.0e6));
        timing.set("averageCallbackMs",
                   JSON(static_cast<double>(telemetry.getAverageCallbackNs()) / 1.0e6));
        timing.set("maxCallbackMs",
                   JSON(static_cast<double>(telemetry.getMaxCallbackNs()) / 1.0e6));
        timing.set("callbackBudgetMs",
                   JSON(static_cast<double>(telemetry.getCallbackBudgetNs()) / 1.0e6));
        timing.set("bufferFrames", JSON(static_cast<double>(telemetry.getLastBufferFrames())));
        timing.set("sampleRate", JSON(static_cast<double>(telemetry.getLastSampleRate())));
        timing.set("deadlineOverruns", JSON(static_cast<double>(overruns)));

        JSON realtime = JSON::object();
        realtime.set("xruns", JSON(static_cast<double>(xruns)));
        realtime.set("underruns", JSON(static_cast<double>(underruns)));
        realtime.set("consecutiveUnderruns",
                     JSON(static_cast<double>(telemetry.getConsecutiveUnderruns())));
        realtime.set("recoveryActive", JSON(recoveryActive));
        realtime.set("recoveryActivations",
                     JSON(static_cast<double>(telemetry.getRecoveryModeActivations())));
        realtime.set("misuseViolations", JSON(static_cast<double>(rtMisuse)));
        realtime.set("threadPriorityStatus",
                     JSON(static_cast<double>(telemetry.getThreadPriorityStatus())));
        realtime.set("threadPriorityOptimal", JSON(telemetry.isThreadPriorityOptimal()));
        realtime.set("linuxPriorityErrno", JSON(static_cast<double>(linuxPriorityErrno)));

        JSON commandQueue = JSON::object();
        commandQueue.set("depth",
                         JSON(static_cast<double>(m_engine->commandQueue().approxDepth())));
        commandQueue.set("maxDepth",
                         JSON(static_cast<double>(m_engine->commandQueue().maxDepth())));
        commandQueue.set("capacity", JSON(static_cast<double>(AudioCommandQueue::capacity())));
        commandQueue.set("dropped", JSON(static_cast<double>(queueDrops)));
        commandQueue.set("edgeDropped", JSON(static_cast<double>(queueEdgeDrops)));

        const uint64_t srcBlocks = telemetry.getSrcActiveBlocks();
        JSON resampling = JSON::object();
        resampling.set("activeBlocks", JSON(static_cast<double>(srcBlocks)));
        resampling.set("activePercent",
                       JSON(blocks > 0 ? 100.0 * static_cast<double>(srcBlocks) /
                                             static_cast<double>(blocks)
                                       : 0.0));

        JSON signal = JSON::object();
        signal.set("nanSamplesSanitized", JSON(static_cast<double>(nanSamples)));
        signal.set("hardClippedSamples", JSON(static_cast<double>(clippedSamples)));

        JSON result = JSON::object();
        result.set("status", JSON(degraded ? "degraded" : observed ? "healthy" : "unobserved"));
        result.set("observed", JSON(observed));
        result.set("counterScope", JSON("engine_lifetime"));
        result.set("issues", issues);
        result.set("timing", timing);
        result.set("realtime", realtime);
        result.set("commandQueue", commandQueue);
        result.set("resampling", resampling);
        result.set("signal", signal);

        JSON response = makeOk();
        response.set("result", result);
        return finish(response);
    }

    if (verb == "get_project_load_report") {
        JSON result = JSON::object();
        if (m_projectLoadReport) {
            result = *m_projectLoadReport;
        } else {
            result.set("status", JSON("unobserved"));
            result.set("observed", JSON(false));
        }
        JSON response = makeOk();
        response.set("result", result);
        return finish(response);
    }

    if (verb == "get_routing_graph") {
        if (!m_trackManager) {
            return makeError(id, "execution_error", "no track manager", verb).toString();
        }

        constexpr uint32_t kMasterSentinel = 0xFFFFFFFFu;
        const auto isMixerMasterTarget = [kMasterSentinel](uint32_t channelId) {
            return channelId == kMasterSentinel;
        };
        const auto mixerRouteNodeId = [&](uint32_t channelId) {
            return isMixerMasterTarget(channelId)
                       ? std::string("master")
                       : std::string("mixer:") + std::to_string(channelId);
        };
        const auto sourceRouteNodeId = [](uint32_t channelId) {
            return channelId == MASTER_MIXER_CHANNEL_ID
                       ? std::string("master")
                       : std::string("mixer:") + std::to_string(channelId);
        };

        const auto channels = m_trackManager->getChannelsSnapshot();
        std::unordered_set<uint32_t> channelIds;
        channelIds.reserve(channels.size());
        for (const auto* channel : channels) {
            if (channel) channelIds.insert(channel->getChannelId());
        }
        const auto mixerTargetResolves = [&](uint32_t channelId) {
            return isMixerMasterTarget(channelId) || channelIds.count(channelId) > 0;
        };
        const auto sourceTargetResolves = [&](uint32_t channelId) {
            return channelId == MASTER_MIXER_CHANNEL_ID || channelIds.count(channelId) > 0;
        };

        JSON sources = JSON::array();
        JSON destinations = JSON::array();
        JSON mainRoutes = JSON::array();
        JSON sends = JSON::array();
        JSON unresolvedRoutes = JSON::array();

        const auto addUnresolved = [&](const char* issueCode, const char* routeType,
                                       const std::string& sourceNodeId, uint32_t targetId,
                                       uint64_t sendId = 0) {
            JSON evidence = JSON::object();
            evidence.set("sourceNodeId", JSON(sourceNodeId));
            evidence.set("targetMixerChannelId", JSON(static_cast<double>(targetId)));
            evidence.set("stableSourceIdentityAvailable", JSON(true));
            evidence.set("stableSendIdAvailable", JSON(sendId != 0));
            if (sendId != 0) evidence.set("sendId", JSON(std::to_string(sendId)));

            JSON issue = JSON::object();
            issue.set("issueCode", JSON(issueCode));
            issue.set("routeType", JSON(routeType));
            issue.set("evidence", evidence);
            unresolvedRoutes.push(issue);
        };

        JSON master = JSON::object();
        master.set("nodeId", JSON("master"));
        master.set("destinationType", JSON("master"));
        master.set("mixerChannelId", JSON(0.0));
        master.set("stableIdentityAvailable", JSON(true));
        if (auto* masterChannel = m_trackManager->getMasterChannel()) {
            master.set("insertChainAvailable", JSON(true));
            const auto& masterChain = masterChannel->getEffectChain();
            JSON pluginSlots = JSON::array();
            for (size_t slotIndex = 0; slotIndex < EffectChain::MAX_SLOTS; ++slotIndex) {
                JSON position = JSON::object();
                position.set("mixerChannelId", JSON(0.0));
                position.set("slotIndex", JSON(static_cast<double>(slotIndex)));

                JSON slot = JSON::object();
                slot.set("slotIndex", JSON(static_cast<double>(slotIndex)));
                slot.set("stableIdentityAvailable", JSON(false));
                slot.set("positionalIdentityAvailable", JSON(true));
                slot.set("position", position);

                if (auto plugin = masterChain.getPlugin(slotIndex)) {
                    slot.set("state", JSON("active"));
                    slot.set("pluginId", JSON(plugin->getInfo().id));
                    slot.set("pluginName", JSON(plugin->getInfo().name));
                    slot.set("bypassed", JSON(masterChain.isSlotBypassed(slotIndex)));
                } else {
                    const std::string missingPluginId = masterChain.getMissingPluginId(slotIndex);
                    if (!missingPluginId.empty()) {
                        slot.set("state", JSON("missing_plugin_placeholder"));
                        slot.set("pluginId", JSON(missingPluginId));
                        slot.set("placeholderPreserved", JSON(true));
                    } else {
                        slot.set("state", JSON("empty"));
                    }
                }
                pluginSlots.push(slot);
            }

            JSON insertChain = JSON::object();
            insertChain.set("slotCount", JSON(static_cast<double>(EffectChain::MAX_SLOTS)));
            insertChain.set("identityKind", JSON("positional"));
            insertChain.set("stableSlotIdentityAvailable", JSON(false));
            insertChain.set("slots", pluginSlots);
            master.set("insertChain", insertChain);
        } else {
            master.set("insertChainAvailable", JSON(false));
        }
        destinations.push(master);

        for (size_t channelIndex = 0; channelIndex < channels.size(); ++channelIndex) {
            const auto* channel = channels[channelIndex];
            if (!channel) continue;
            const uint32_t channelId = channel->getChannelId();
            const std::string sourceNodeId = mixerRouteNodeId(channelId);

            JSON pluginSlots = JSON::array();
            const auto& chain = channel->getEffectChain();
            for (size_t slotIndex = 0; slotIndex < EffectChain::MAX_SLOTS; ++slotIndex) {
                JSON position = JSON::object();
                position.set("mixerChannelId", JSON(static_cast<double>(channelId)));
                position.set("slotIndex", JSON(static_cast<double>(slotIndex)));

                JSON slot = JSON::object();
                slot.set("slotIndex", JSON(static_cast<double>(slotIndex)));
                slot.set("stableIdentityAvailable", JSON(false));
                slot.set("positionalIdentityAvailable", JSON(true));
                slot.set("position", position);

                if (auto plugin = chain.getPlugin(slotIndex)) {
                    slot.set("state", JSON("active"));
                    slot.set("pluginId", JSON(plugin->getInfo().id));
                    slot.set("pluginName", JSON(plugin->getInfo().name));
                    slot.set("bypassed", JSON(chain.isSlotBypassed(slotIndex)));
                } else {
                    const std::string missingPluginId = chain.getMissingPluginId(slotIndex);
                    if (!missingPluginId.empty()) {
                        slot.set("state", JSON("missing_plugin_placeholder"));
                        slot.set("pluginId", JSON(missingPluginId));
                        slot.set("placeholderPreserved", JSON(true));
                    } else {
                        slot.set("state", JSON("empty"));
                    }
                }
                pluginSlots.push(slot);
            }

            JSON insertChain = JSON::object();
            insertChain.set("slotCount", JSON(static_cast<double>(EffectChain::MAX_SLOTS)));
            insertChain.set("identityKind", JSON("positional"));
            insertChain.set("stableSlotIdentityAvailable", JSON(false));
            insertChain.set("slots", pluginSlots);

            JSON destination = JSON::object();
            destination.set("nodeId", JSON(sourceNodeId));
            destination.set("destinationType", JSON("mixer_channel"));
            destination.set("mixerChannelId", JSON(static_cast<double>(channelId)));
            destination.set("name", JSON(channel->getName()));
            destination.set("index", JSON(static_cast<double>(channelIndex)));
            destination.set("stableIdentityAvailable", JSON(true));
            destination.set("insertChainAvailable", JSON(true));
            destination.set("insertChain", insertChain);
            destinations.push(destination);

            const uint32_t mainTargetId = channel->getMainOutputId();
            const bool mainResolved = mixerTargetResolves(mainTargetId);
            JSON mainRoute = JSON::object();
            mainRoute.set("routeType", JSON("main"));
            mainRoute.set("sourceNodeId", JSON(sourceNodeId));
            mainRoute.set("sourceMixerChannelId", JSON(static_cast<double>(channelId)));
            mainRoute.set("targetMixerChannelId",
                          JSON(static_cast<double>(isMixerMasterTarget(mainTargetId) ? 0u
                                                                                   : mainTargetId)));
            mainRoute.set("resolved", JSON(mainResolved));
            if (mainResolved) {
                mainRoute.set("destinationNodeId", JSON(mixerRouteNodeId(mainTargetId)));
            }
            mainRoutes.push(mainRoute);
            if (!mainResolved) {
                addUnresolved("unresolved_main_destination", "main", sourceNodeId, mainTargetId);
            }

            const auto channelSends = channel->getSends();
            for (size_t sendIndex = 0; sendIndex < channelSends.size(); ++sendIndex) {
                const auto& route = channelSends[sendIndex];
                if (!std::isfinite(route.gain) || !std::isfinite(route.pan)) {
                    return makeError(id, "execution_error",
                                     "mixer channel " + std::to_string(channelId) +
                                         " send " + std::to_string(sendIndex) +
                                         " has non-finite routing values",
                                     verb)
                        .toString();
                }
                const bool sendResolved = mixerTargetResolves(route.targetChannelId);
                JSON send = JSON::object();
                send.set("routeType", JSON(route.sidechainOnly ? "sidechain_send" : "send"));
                send.set("sourceNodeId", JSON(sourceNodeId));
                send.set("sourceMixerChannelId", JSON(static_cast<double>(channelId)));
                send.set("sendId", JSON(std::to_string(route.sendId)));
                send.set("stableIdentityAvailable", JSON(route.sendId != 0));
                send.set("positionalIdentityAvailable", JSON(false));
                send.set("targetMixerChannelId",
                         JSON(static_cast<double>(isMixerMasterTarget(route.targetChannelId)
                                                      ? 0u
                                                      : route.targetChannelId)));
                send.set("resolved", JSON(sendResolved));
                if (sendResolved) {
                    send.set("destinationNodeId", JSON(mixerRouteNodeId(route.targetChannelId)));
                }
                send.set("gain", JSON(static_cast<double>(route.gain)));
                send.set("pan", JSON(static_cast<double>(route.pan)));
                send.set("postFader", JSON(route.postFader));
                send.set("muted", JSON(route.mute));
                send.set("sidechainOnly", JSON(route.sidechainOnly));
                send.set("sendId", JSON(std::to_string(route.sendId)));
                sends.push(send);
                if (!sendResolved) {
                    addUnresolved("unresolved_send_destination",
                                  route.sidechainOnly ? "sidechain_send" : "send",
                                  sourceNodeId, route.targetChannelId,
                                  route.sendId);
                }
            }
        }

        const auto& unitManager = m_trackManager->getUnitManager();
        for (const UnitID unitId : unitManager.getAllUnitIDs()) {
            const auto* unit = unitManager.getUnit(unitId);
            if (!unit) continue;
            const uint32_t targetId = unitManager.getUnitMixerChannel(unitId);
            const bool resolved = sourceTargetResolves(targetId);
            const std::string sourceNodeId = "unit:" + std::to_string(unitId);

            JSON destination = JSON::object();
            destination.set("targetMixerChannelId", JSON(static_cast<double>(targetId)));
            destination.set("resolved", JSON(resolved));
            if (resolved) destination.set("nodeId", JSON(sourceRouteNodeId(targetId)));

            JSON source = JSON::object();
            source.set("nodeId", JSON(sourceNodeId));
            source.set("sourceType", JSON("unit"));
            source.set("unitId", JSON(std::to_string(unitId)));
            source.set("name", JSON(unit->name));
            source.set("unitType", JSON(unitTypeName(unit->type)));
            source.set("stableIdentityAvailable", JSON(true));
            source.set("destination", destination);
            sources.push(source);

            if (!resolved) {
                addUnresolved("unresolved_unit_destination", "source", sourceNodeId, targetId);
            }
        }

        auto patterns = m_trackManager->getPatternManager().getAllPatterns();
        std::sort(patterns.begin(), patterns.end(), [](const auto& lhs, const auto& rhs) {
            if (!lhs) return false;
            if (!rhs) return true;
            return lhs->id.value < rhs->id.value;
        });
        for (const auto& pattern : patterns) {
            if (!pattern || !pattern->isAudio()) continue;
            const uint32_t targetId = pattern->getMixerChannelId();
            const bool resolved = sourceTargetResolves(targetId);
            const std::string patternId = std::to_string(pattern->id.value);
            const std::string sourceNodeId = "audio_pattern:" + patternId;

            JSON destination = JSON::object();
            destination.set("targetMixerChannelId", JSON(static_cast<double>(targetId)));
            destination.set("resolved", JSON(resolved));
            if (resolved) destination.set("nodeId", JSON(sourceRouteNodeId(targetId)));

            JSON source = JSON::object();
            source.set("nodeId", JSON(sourceNodeId));
            source.set("sourceType", JSON("audio_pattern"));
            source.set("patternId", JSON(patternId));
            source.set("name", JSON(pattern->name));
            source.set("stableIdentityAvailable", JSON(true));
            source.set("destination", destination);
            sources.push(source);

            if (!resolved) {
                addUnresolved("unresolved_audio_pattern_destination", "source",
                              sourceNodeId, targetId);
            }
        }

        JSON identityPolicy = JSON::object();
        identityPolicy.set("mixerChannels", JSON("stable_id"));
        identityPolicy.set("units", JSON("stable_id"));
        identityPolicy.set("audioPatterns", JSON("stable_id"));
        identityPolicy.set("pluginSlots", JSON("positional"));
        identityPolicy.set("sends", JSON("stable_id"));

        JSON result = JSON::object();
        result.set("status", JSON(unresolvedRoutes.size() > 0 ? "degraded" : "resolved"));
        result.set("authority", JSON("project_model"));
        result.set("identityPolicy", identityPolicy);
        result.set("sources", sources);
        result.set("destinations", destinations);
        result.set("mainRoutes", mainRoutes);
        result.set("sends", sends);
        result.set("unresolvedRoutes", unresolvedRoutes);

        JSON response = makeOk();
        response.set("result", result);
        return finish(response);
    }

    if (verb == "get_latency_report") {
        JSON result = JSON::object();
        JSON nodes = JSON::array();
        JSON edges = JSON::array();
        JSON uncompensatedPaths = JSON::array();
        JSON mismatches = JSON::array();
        JSON warnings = JSON::array();
        JSON graphMaximum = JSON::object();
        graphMaximum.set("projectAlignmentSamples", JSON(0.0));
        graphMaximum.set("monitoringLatencySamples", JSON(0.0));
        graphMaximum.set("engineMaxProjectLatencySamples", JSON(0.0));

        const auto complete = [&]() {
            result.set("graphMaximum", graphMaximum);
            result.set("nodes", nodes);
            result.set("edges", edges);
            result.set("uncompensatedPaths", uncompensatedPaths);
            result.set("mismatches", mismatches);
            result.set("warnings", warnings);
            JSON response = makeOk();
            response.set("result", result);
            return finish(response);
        };

        if (!m_engine) {
            result.set("status", JSON("unobserved"));
            result.set("observed", JSON(false));
            result.set("authority", JSON("audio_engine_pdc"));
            result.set("compensationEnabled", JSON(false));
            result.set("recalculationPending", JSON(false));
            result.set("generation", JSON("0"));
            return complete();
        }

        constexpr uint32_t kMasterSentinel = 0xFFFFFFFFu;
        const auto nodeId = [kMasterSentinel](uint32_t channelId) {
            return channelId == kMasterSentinel ? std::string("master")
                                                : std::string("mixer:") + std::to_string(channelId);
        };
        const auto topology = m_engine->getLastSolvedLatencyTopology();
        const bool observed = topology.generation > 0;
        const bool compensationEnabled = m_engine->isLatencyCompensationEnabled();
        const bool recalculationPending = m_engine->isLatencyRecalculationPending();

        result.set("observed", JSON(observed));
        result.set("authority", JSON("audio_engine_pdc"));
        result.set("compensationEnabled", JSON(compensationEnabled));
        result.set("recalculationPending", JSON(recalculationPending));
        result.set("generation", JSON(std::to_string(topology.generation)));
        if (!observed) {
            result.set("status", JSON("unobserved"));
            return complete();
        }

        const auto addMismatch = [&](const char* issueCode, const std::string& message, JSON evidence) {
            JSON issue = JSON::object();
            issue.set("issueCode", JSON(issueCode));
            issue.set("message", JSON(message));
            issue.set("evidence", evidence);
            mismatches.push(issue);
        };

        std::vector<MixerChannel*> channels;
        if (m_trackManager)
            channels = m_trackManager->getChannelsSnapshot();

        std::unordered_map<uint32_t, size_t> currentTrackByChannelId;
        currentTrackByChannelId.reserve(channels.size());
        for (size_t i = 0; i < channels.size(); ++i) {
            if (channels[i])
                currentTrackByChannelId[channels[i]->getChannelId()] = i;
        }

        std::unordered_map<uint32_t, size_t> topologyNodeByChannelId;
        topologyNodeByChannelId.reserve(topology.nodes.size());
        for (size_t i = 0; i < topology.nodes.size(); ++i) {
            topologyNodeByChannelId[topology.nodes[i].channelId] = i;
        }

        if (!m_trackManager) {
            JSON evidence = JSON::object();
            evidence.set("generation", JSON(std::to_string(topology.generation)));
            evidence.set("stableIdentityAvailable", JSON(false));
            evidence.set("positionalIdentityAvailable", JSON(false));
            addMismatch("pdc_project_model_unavailable",
                        "the solved topology cannot be compared with the current project model", evidence);
        }

        for (const auto& solution : topology.nodes) {
            const bool master = solution.channelId == kMasterSentinel;
            const std::string stableNodeId = nodeId(solution.channelId);
            bool mismatch = false;

            JSON node = JSON::object();
            node.set("nodeId", JSON(stableNodeId));
            node.set("nodeType", JSON(master ? "master" : "mixer_channel"));
            if (!master) {
                node.set("mixerChannelId", JSON(static_cast<double>(solution.channelId)));
            }
            node.set("stableIdentityAvailable", JSON(true));
            node.set("intrinsicLatencySamples", JSON(static_cast<double>(solution.intrinsicLatency)));
            node.set("downstreamLatencySamples", JSON(static_cast<double>(solution.downstreamLatency)));
            node.set("totalPathLatencySamples", JSON(static_cast<double>(solution.totalPathLatency)));
            node.set("outputCompensationSamples", JSON(static_cast<double>(solution.outputCompensationSamples)));

            JSON applied = JSON::object();
            applied.set("available", JSON(false));
            if (!master) {
                const auto trackIt = currentTrackByChannelId.find(solution.channelId);
                if (trackIt == currentTrackByChannelId.end()) {
                    JSON evidence = JSON::object();
                    evidence.set("nodeId", JSON(stableNodeId));
                    evidence.set("stableIdentityAvailable", JSON(true));
                    evidence.set("positionalIdentityAvailable", JSON(false));
                    addMismatch("pdc_node_missing_from_project",
                                "a solved PDC node is absent from the current project model", evidence);
                    mismatch = true;
                } else {
                    const size_t trackIndex = trackIt->second;
                    const auto snapshot = m_engine->getTrackEdgeDelaySnapshot(trackIndex);
                    const uint32_t currentIntrinsic = channels[trackIndex]->getEffectChain().getTotalLatency();
                    applied.set("available", JSON(snapshot.valid));
                    applied.set("currentIntrinsicLatencySamples", JSON(static_cast<double>(currentIntrinsic)));
                    if (snapshot.valid) {
                        applied.set("intrinsicLatencySamples",
                                    JSON(static_cast<double>(snapshot.pluginLatencySamples)));
                        applied.set("outputCompensationSamples",
                                    JSON(static_cast<double>(snapshot.outputCompensationSamples)));
                        applied.set("compensationEnabled", JSON(snapshot.compensationEnabled));
                        if (snapshot.pluginLatencySamples != solution.intrinsicLatency ||
                            snapshot.outputCompensationSamples != solution.outputCompensationSamples) {
                            JSON evidence = JSON::object();
                            evidence.set("nodeId", JSON(stableNodeId));
                            evidence.set("stableIdentityAvailable", JSON(true));
                            evidence.set("positionalIdentityAvailable", JSON(false));
                            evidence.set("solvedIntrinsicLatencySamples",
                                         JSON(static_cast<double>(solution.intrinsicLatency)));
                            evidence.set("appliedIntrinsicLatencySamples",
                                         JSON(static_cast<double>(snapshot.pluginLatencySamples)));
                            evidence.set("solvedOutputCompensationSamples",
                                         JSON(static_cast<double>(solution.outputCompensationSamples)));
                            evidence.set("appliedOutputCompensationSamples",
                                         JSON(static_cast<double>(snapshot.outputCompensationSamples)));
                            addMismatch("pdc_node_application_mismatch",
                                        "the RT-side node delay does not match the solved topology", evidence);
                            mismatch = true;
                        }
                    }
                    if (currentIntrinsic != solution.intrinsicLatency) {
                        JSON evidence = JSON::object();
                        evidence.set("nodeId", JSON(stableNodeId));
                        evidence.set("stableIdentityAvailable", JSON(true));
                        evidence.set("positionalIdentityAvailable", JSON(false));
                        evidence.set("solvedIntrinsicLatencySamples",
                                     JSON(static_cast<double>(solution.intrinsicLatency)));
                        evidence.set("currentIntrinsicLatencySamples", JSON(static_cast<double>(currentIntrinsic)));
                        addMismatch("pdc_node_intrinsic_latency_stale",
                                    "the current plugin chain latency differs from the published solve", evidence);
                        mismatch = true;
                    }
                }
            }
            node.set("applied", applied);
            node.set("mismatch", JSON(mismatch));
            nodes.push(node);
        }

        struct CurrentEdge {
            uint32_t srcNodeIdx{0};
            uint32_t dstNodeIdx{0};
            bool sidechain{false};
            size_t trackIndex{0};
            size_t sendIndex{0}; // positional index into the RT send-edge-delay snapshot
            uint64_t sendId{0};  // stable send identity (Contract D2)
            bool main{false};
        };
        std::vector<CurrentEdge> currentEdges;
        for (size_t trackIndex = 0; trackIndex < channels.size(); ++trackIndex) {
            const auto* channel = channels[trackIndex];
            if (!channel)
                continue;
            const auto src = topologyNodeByChannelId.find(channel->getChannelId());
            if (src == topologyNodeByChannelId.end())
                continue;

            const auto main = topologyNodeByChannelId.find(channel->getMainOutputId());
            if (main != topologyNodeByChannelId.end() && main->second != src->second) {
                currentEdges.push_back({static_cast<uint32_t>(src->second), static_cast<uint32_t>(main->second),
                                        false, trackIndex, 0, 0, true});
            }
            const auto sends = channel->getSends();
            for (size_t sendIndex = 0; sendIndex < sends.size(); ++sendIndex) {
                const auto& send = sends[sendIndex];
                if (send.mute)
                    continue;
                const auto destination = topologyNodeByChannelId.find(send.targetChannelId);
                if (destination == topologyNodeByChannelId.end() || destination->second == src->second) {
                    continue;
                }
                currentEdges.push_back({static_cast<uint32_t>(src->second),
                                        static_cast<uint32_t>(destination->second), send.sidechainOnly, trackIndex,
                                        sendIndex, send.sendId, false});
            }
        }

        // Match solved edges to current routing by identity, not position.
        // Pairing solved edge `i` with currentEdges[i] was only correct
        // while the model was unchanged: inserting or removing one route
        // shifted everything after it, so a single routing edit reported
        // pdc_edge_mapping_mismatch on every subsequent edge instead of
        // the one that actually moved. pdc_edge_count_mismatch already
        // reports that the shapes differ.
        //
        // (src, dst, sidechain) is not unique — a channel may hold two
        // sends to the same target — so equal keys are consumed in
        // discovery order, which keeps the pairing deterministic and
        // still localizes a mismatch to the edge that changed.
        // Mixed-radix rather than bit-packed: both indices are bounded by
        // nodes.size(), so this is exact and collision-free without
        // assuming either fits in a fixed bit width.
        const uint64_t nodeCount = static_cast<uint64_t>(topology.nodes.size());
        const auto edgeKey = [nodeCount](uint32_t src, uint32_t dst, bool sidechain) -> uint64_t {
            return ((static_cast<uint64_t>(src) * nodeCount) + static_cast<uint64_t>(dst)) * 2ull +
                   (sidechain ? 1ull : 0ull);
        };
        std::unordered_map<uint64_t, std::vector<size_t>> currentEdgesByKey;
        for (size_t j = 0; j < currentEdges.size(); ++j) {
            currentEdgesByKey[edgeKey(currentEdges[j].srcNodeIdx, currentEdges[j].dstNodeIdx,
                                      currentEdges[j].sidechain)]
                .push_back(j);
        }
        std::unordered_map<uint64_t, size_t> currentEdgeCursor;
        constexpr size_t kNoCurrentEdge = static_cast<size_t>(-1);

        for (size_t i = 0; i < topology.edges.size(); ++i) {
            const auto& solution = topology.edges[i];
            const bool sourceValid = solution.srcNodeIdx < topology.nodes.size();
            const bool destinationValid = solution.dstNodeIdx < topology.nodes.size();
            const std::string sourceNodeId = sourceValid ? nodeId(topology.nodes[solution.srcNodeIdx].channelId)
                                                         : "unknown:" + std::to_string(solution.srcNodeIdx);
            const std::string destinationNodeId = destinationValid
                                                      ? nodeId(topology.nodes[solution.dstNodeIdx].channelId)
                                                      : "unknown:" + std::to_string(solution.dstNodeIdx);

            size_t currentEdgeIndex = kNoCurrentEdge;
            if (sourceValid && destinationValid) {
                const uint64_t key = edgeKey(solution.srcNodeIdx, solution.dstNodeIdx, solution.sidechain);
                const auto candidates = currentEdgesByKey.find(key);
                if (candidates != currentEdgesByKey.end()) {
                    auto& cursor = currentEdgeCursor[key];
                    if (cursor < candidates->second.size()) {
                        currentEdgeIndex = candidates->second[cursor++];
                    }
                }
            }
            const bool mappingMatches = currentEdgeIndex != kNoCurrentEdge;
            bool main = false;
            size_t sendIndex = 0;
            uint64_t sendId = 0;
            bool appliedAvailable = false;
            uint32_t appliedCompensation = 0;
            bool mismatch = !mappingMatches;

            if (mappingMatches) {
                const auto& current = currentEdges[currentEdgeIndex];
                main = current.main;
                sendIndex = current.sendIndex;
                sendId = current.sendId;
                const auto snapshot = m_engine->getTrackEdgeDelaySnapshot(current.trackIndex);
                if (snapshot.valid && main) {
                    appliedCompensation = snapshot.mainOutEdgeDelay.compensationSamples;
                    appliedAvailable = true;
                } else if (snapshot.valid && sendIndex < snapshot.sendEdgeDelays.size()) {
                    appliedCompensation = snapshot.sendEdgeDelays[sendIndex].compensationSamples;
                    appliedAvailable = true;
                }
                mismatch = !appliedAvailable || appliedCompensation != solution.compensationSamples;
            }

            if (mismatch) {
                JSON evidence = JSON::object();
                evidence.set("edgeIndex", JSON(static_cast<double>(i)));
                evidence.set("sourceNodeId", JSON(sourceNodeId));
                evidence.set("destinationNodeId", JSON(destinationNodeId));
                evidence.set("stableEndpointIdentityAvailable", JSON(sourceValid && destinationValid));
                evidence.set("stableSendIdAvailable", JSON(mappingMatches && !main && sendId != 0));
                if (mappingMatches && !main && sendId != 0) {
                    evidence.set("sendId", JSON(std::to_string(sendId)));
                }
                evidence.set("solvedCompensationSamples", JSON(static_cast<double>(solution.compensationSamples)));
                evidence.set("appliedCompensationAvailable", JSON(appliedAvailable));
                evidence.set("appliedCompensationSamples", JSON(static_cast<double>(appliedCompensation)));
                addMismatch(mappingMatches ? "pdc_edge_application_mismatch" : "pdc_edge_mapping_mismatch",
                            mappingMatches ? "the RT-side edge delay does not match the solved topology"
                                           : "the solved edge cannot be mapped to the current routing model",
                            evidence);
            }

            JSON edge = JSON::object();
            edge.set("edgeIndex", JSON(static_cast<double>(i)));
            edge.set("sourceNodeId", JSON(sourceNodeId));
            edge.set("destinationNodeId", JSON(destinationNodeId));
            edge.set("stableEndpointIdentityAvailable", JSON(sourceValid && destinationValid));
            const char* routeType = solution.sidechain ? "sidechain_send"
                                    : !mappingMatches  ? "unresolved"
                                    : main             ? "main"
                                                       : "send";
            edge.set("routeType", JSON(routeType));
            edge.set("sidechainOnly", JSON(solution.sidechain));
            edge.set("solverCompensationSamples", JSON(static_cast<double>(solution.compensationSamples)));
            edge.set("appliedCompensationAvailable", JSON(appliedAvailable));
            edge.set("appliedCompensationSamples", JSON(static_cast<double>(appliedCompensation)));
            edge.set("stableIdentityAvailable", JSON(mappingMatches && !main && sendId != 0));
            if (mappingMatches && !main && sendId != 0) {
                edge.set("sendId", JSON(std::to_string(sendId)));
            }
            edge.set("mismatch", JSON(mismatch));
            edges.push(edge);

            if (solution.sidechain) {
                JSON evidence = JSON::object();
                evidence.set("sourceNodeId", JSON(sourceNodeId));
                evidence.set("destinationNodeId", JSON(destinationNodeId));
                evidence.set("stableEndpointIdentityAvailable", JSON(sourceValid && destinationValid));
                evidence.set("stableSendIdAvailable", JSON(mappingMatches && !main && sendId != 0));
                if (mappingMatches && !main && sendId != 0) {
                    evidence.set("sendId", JSON(std::to_string(sendId)));
                }
                JSON issue = JSON::object();
                issue.set("issueCode", JSON("sidechain_latency_compensation_unavailable"));
                issue.set("message", JSON("sidechain paths are excluded from the current PDC solve"));
                issue.set("evidence", evidence);
                uncompensatedPaths.push(issue);
            }
        }

        if (currentEdges.size() != topology.edges.size()) {
            JSON evidence = JSON::object();
            evidence.set("solvedEdgeCount", JSON(static_cast<double>(topology.edges.size())));
            evidence.set("currentMappableEdgeCount", JSON(static_cast<double>(currentEdges.size())));
            evidence.set("stableIdentityAvailable", JSON(false));
            evidence.set("positionalIdentityAvailable", JSON(false));
            addMismatch("pdc_edge_count_mismatch",
                        "the current routing model and published solve have different edge counts", evidence);
        }
        if (m_engine->getMaxProjectLatency() != topology.projectAlignmentLatency) {
            JSON evidence = JSON::object();
            evidence.set("solvedProjectAlignmentSamples",
                         JSON(static_cast<double>(topology.projectAlignmentLatency)));
            evidence.set("engineMaxProjectLatencySamples",
                         JSON(static_cast<double>(m_engine->getMaxProjectLatency())));
            evidence.set("stableIdentityAvailable", JSON(false));
            evidence.set("positionalIdentityAvailable", JSON(false));
            addMismatch("pdc_graph_maximum_mismatch", "the engine maximum does not match the published topology",
                        evidence);
        }
        if (recalculationPending) {
            JSON evidence = JSON::object();
            evidence.set("generation", JSON(std::to_string(topology.generation)));
            evidence.set("stableIdentityAvailable", JSON(false));
            evidence.set("positionalIdentityAvailable", JSON(false));
            addMismatch("pdc_recalculation_pending", "the published topology is pending recalculation", evidence);
        }

        // Map the solver's classification onto this report's stable issue
        // codes. Switching on the enum keeps the contract structural: a
        // reworded diagnostic can no longer silently demote a specific
        // code to the generic fallback, and adding a SolverWarningCode
        // shows up here as an unhandled-enum warning rather than as a
        // string that quietly stops matching.
        const auto issueCodeFor = [](SolverWarningCode code) -> const char* {
            switch (code) {
            case SolverWarningCode::RoutingCycle:
                return "pdc_routing_cycle";
            case SolverWarningCode::InvalidEdgeIndices:
                return "pdc_invalid_edge_indices";
            }
            return "pdc_solver_warning";
        };

        for (const auto& solverWarning : topology.warnings) {
            JSON warning = JSON::object();
            warning.set("issueCode", JSON(issueCodeFor(solverWarning.code)));
            warning.set("message", JSON(solverWarning.message));
            warning.set("stableIdentityAvailable", JSON(false));
            warning.set("positionalIdentityAvailable", JSON(false));
            warnings.push(warning);
        }

        graphMaximum.set("projectAlignmentSamples", JSON(static_cast<double>(topology.projectAlignmentLatency)));
        graphMaximum.set("monitoringLatencySamples", JSON(static_cast<double>(topology.monitoringLatency)));
        graphMaximum.set("engineMaxProjectLatencySamples",
                         JSON(static_cast<double>(m_engine->getMaxProjectLatency())));
        const bool degraded = mismatches.size() > 0 || uncompensatedPaths.size() > 0 || warnings.size() > 0;
        result.set("status", JSON(!compensationEnabled ? "disabled" : degraded ? "degraded" : "clean"));
        return complete();
    }
    // No verb matched. In the single-file version this fell out of the if-chain
    // into the next family's checks and eventually the mutation path, which is
    // exactly what nullopt asks handleRequest to do.
    return std::nullopt;
}

} // namespace MuseInternal
} // namespace Audio
} // namespace Aestra
