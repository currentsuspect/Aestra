// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Core/AudioEngine.h"

namespace Aestra {
namespace Audio {

/**
 * @brief RAII hold on AudioEngine::beginManualRender() for a render driven by hand.
 *
 * Export, bounce and Muse renders call processBlock() themselves. Run while the device
 * stream is live, that put two threads inside the same engine at once — the master and
 * track buffers, the note queue and every plugin shared between them. Holding a lease
 * makes the live callback play silence for its duration. The lease waits for any live
 * block in flight to finish, for up to 2 s; if a block is still running after that, the
 * wait is abandoned (and logged), and exclusion is not guaranteed.
 */
class ManualRenderLease {
public:
    explicit ManualRenderLease(AudioEngine& engine) : m_engine(engine) { m_engine.beginManualRender(); }
    ~ManualRenderLease() { m_engine.endManualRender(); }
    ManualRenderLease(const ManualRenderLease&) = delete;
    ManualRenderLease& operator=(const ManualRenderLease&) = delete;

private:
    AudioEngine& m_engine;
};

} // namespace Audio
} // namespace Aestra
