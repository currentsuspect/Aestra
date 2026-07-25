// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Regression test for #611: undoing a track delete used to re-create the
// channel rather than restore it.
//
// Driven at the command layer, not through Muse, because that is where the bug
// lives: the same sequence is reachable from the UI's Ctrl+Z. The original
// DeleteTrackCommand::undo() called addChannel(), which mints a NEW MixerChannel
// with a NEW id at the END of the vector, so undoing a delete:
//   - left every older command holding `MixerChannel&` dangling (ASan: heap-use-
//     after-free at std::atomic<float>::exchange),
//   - changed the channel id that routing is keyed on,
//   - moved the track to the wrong index,
//   - and restored only the name, dropping volume/pan/mute/solo.
//
// Under a normal build the first of those is silent corruption, which is why it
// went unnoticed. The undo-past-a-delete step below is the one that tripped ASan.

#include "Commands/AddChannelCommand.h"
#include "Commands/CommandHistory.h"
#include "Commands/MuseStubs.h"
#include "Commands/SetPanCommand.h"
#include "Commands/SetVolumeCommand.h"
#include "Models/TrackManager.h"
#include "RealtimeThreadGuard.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <string>

using Aestra::Audio::CommandHistory;
using Aestra::Audio::DeleteTrackCommand;
using Aestra::Audio::MixerChannel;
using Aestra::Audio::SetPanCommand;
using Aestra::Audio::SetVolumeCommand;
using Aestra::Audio::TrackManager;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& label) {
    if (condition) {
        std::cout << "PASS: " << label << "\n";
    } else {
        std::cout << "FAIL: " << label << "\n";
        ++g_failures;
    }
}

// Not `near`: that is a legacy Windows macro (minwindef.h), so MSVC rejects a
// function of that name outright.
bool almostEqual(float a, float b) { return std::fabs(a - b) < 1e-5f; }

} // namespace

int main() {
    TrackManager manager;
    CommandHistory history;

    MixerChannel* first = manager.addChannel("First");
    MixerChannel* victim = manager.addChannel("Victim");
    MixerChannel* last = manager.addChannel("Last");
    if (!first || !victim || !last) {
        std::cout << "FAIL: could not build the fixture\n";
        return 1;
    }

    const uint32_t victimId = victim->getChannelId();
    const uint32_t lastId = last->getChannelId();

    // Give the doomed track state that only lives in the object, so a
    // re-created replacement would be detectably different.
    history.pushAndExecute(std::make_shared<SetVolumeCommand>(*victim, 0.25f));
    history.pushAndExecute(std::make_shared<SetPanCommand>(*victim, -0.5f));
    check(almostEqual(victim->getVolume(), 0.25f), "fixture: volume applied");
    check(almostEqual(victim->getPan(), -0.5f), "fixture: pan applied");

    // Delete the middle track.
    history.pushAndExecute(std::make_shared<DeleteTrackCommand>(manager, 1));
    check(manager.getChannelCount() == 2, "delete removed the track");
    check(manager.getChannel(1) != nullptr && manager.getChannel(1)->getChannelId() == lastId,
          "the track after it shifted down");

    // --- undo the delete: the SAME channel must come back -------------------
    check(history.undo(), "undo of delete reported success");
    check(manager.getChannelCount() == 3, "the track is back");

    MixerChannel* restored = manager.getChannel(1);
    check(restored != nullptr, "restored at its original index, not appended");
    if (restored != nullptr) {
        check(restored->getChannelId() == victimId,
              "restored with its ORIGINAL channel id (routing points at ids, not indexes)");
        check(restored->getName() == "Victim", "restored with its name");
        check(almostEqual(restored->getVolume(), 0.25f), "restored with its volume, not a default");
        check(almostEqual(restored->getPan(), -0.5f), "restored with its pan, not a default");
        check(restored == victim,
              "restored the SAME object, so references held by older commands stay valid");
    }
    check(manager.getChannel(2) != nullptr && manager.getChannel(2)->getChannelId() == lastId,
          "the track after it moved back down");

    // --- undo past the delete: this is the step that used to be a UAF -------
    // Older commands hold `MixerChannel&` to the channel the delete removed.
    // With a re-created channel those references dangled; ASan caught the write.
    check(history.undo(), "undo of the pan change reported success");
    check(restored != nullptr && almostEqual(restored->getPan(), 0.0f),
          "pan undone through a reference that survived the delete");
    check(history.undo(), "undo of the volume change reported success");
    check(restored != nullptr && almostEqual(restored->getVolume(), 1.0f),
          "volume undone through a reference that survived the delete");

    // --- redo forward again -------------------------------------------------
    check(history.redo() && almostEqual(restored->getVolume(), 0.25f), "redo re-applies the volume");
    check(history.redo() && almostEqual(restored->getPan(), -0.5f), "redo re-applies the pan");
    check(history.redo(), "redo re-applies the delete");
    check(manager.getChannelCount() == 2, "the track is deleted again");

    // And a second round trip still restores the same identity, so the fix is
    // not a one-shot that works only until the channel has been detached twice.
    check(history.undo(), "undo of the re-applied delete");
    check(manager.getChannelCount() == 3 && manager.getChannel(1) != nullptr &&
              manager.getChannel(1)->getChannelId() == victimId,
          "a second delete/undo round trip preserves the id");

    // --- deleting the last track restores to the end ------------------------
    {
        TrackManager tail;
        CommandHistory tailHistory;
        tail.addChannel("A");
        MixerChannel* b = tail.addChannel("B");
        const uint32_t bId = b->getChannelId();
        tailHistory.pushAndExecute(std::make_shared<DeleteTrackCommand>(tail, 1));
        check(tail.getChannelCount() == 1, "tail delete removed the last track");
        check(tailHistory.undo() && tail.getChannelCount() == 2, "tail delete undone");
        check(tail.getChannel(1) != nullptr && tail.getChannel(1)->getChannelId() == bId,
              "a deleted last track comes back at the end with its id");
    }

    // --- the mirror case: AddChannelCommand undo/redo -----------------------
    // Same defect, opposite direction. undo() used to destroy the channel and
    // redo() built a fresh one with a new id, so add / edit / undo / undo /
    // redo / redo wrote the edit through a dangling reference.
    {
        TrackManager added;
        CommandHistory addHistory;

        addHistory.pushAndExecute(std::make_shared<Aestra::Audio::AddChannelCommand>(added, "Added"));
        MixerChannel* channel = added.getChannel(0);
        check(channel != nullptr, "add created a channel");
        const uint32_t addedId = channel->getChannelId();

        addHistory.pushAndExecute(std::make_shared<SetVolumeCommand>(*channel, 0.4f));
        check(almostEqual(channel->getVolume(), 0.4f), "volume applied to the added channel");

        check(addHistory.undo(), "undo of the volume change");
        check(addHistory.undo(), "undo of the add");
        check(added.getChannelCount() == 0, "the added channel is gone");

        check(addHistory.redo(), "redo of the add");
        MixerChannel* back = added.getChannel(0);
        check(back != nullptr && back->getChannelId() == addedId,
              "a redone add restores the ORIGINAL id, not a fresh one");
        check(back == channel, "and the SAME object, so the volume command's reference is valid");

        // The step that used to write through freed memory.
        check(addHistory.redo(), "redo of the volume change");
        check(back != nullptr && almostEqual(back->getVolume(), 0.4f),
              "volume re-applied through a reference that survived the undo");
    }

    // --- a refused reinsert must not eat the channel ------------------------
    // reinsertChannel takes the unique_ptr by reference and moves from it only
    // on success. By value, the caller's pointer would already be moved-from at
    // the call site, so any refusal inside the function would destroy the
    // channel with the caller holding nothing — the same "the channel is gone"
    // defect this whole change exists to prevent, merely relocated.
    {
        TrackManager guarded;
        guarded.addChannel("Keep");
        size_t index = 0;
        const uint32_t keptId = guarded.getChannel(0)->getChannelId();
        std::unique_ptr<MixerChannel> detached = guarded.detachChannelById(keptId, index);
        check(detached != nullptr, "detached the channel for the refusal case");
        check(guarded.getChannelCount() == 0, "and it left the track list");

        {
            // Pretend to be the audio thread, with a no-op handler so the debug
            // assert inside reportRealtimeMisuse does not fire.
            auto previous = Aestra::Audio::setRealtimeMisuseHandler(+[](const char*) noexcept {});
            Aestra::Audio::ScopedRealtimeAudioThread pretendAudioThread;
            const bool inserted = guarded.reinsertChannel(detached, index);
            check(!inserted, "reinsert is refused on the audio thread");
            Aestra::Audio::setRealtimeMisuseHandler(previous);
        }

        check(detached != nullptr, "the caller STILL owns the channel after a refusal");
        check(guarded.getChannelCount() == 0, "and nothing was inserted");

        // Off the audio thread it goes back, same object, same id.
        MixerChannel* raw = detached.get();
        check(guarded.reinsertChannel(detached, index), "the retry succeeds");
        check(detached == nullptr, "ownership transferred only on success");
        check(guarded.getChannelCount() == 1 && guarded.getChannel(0) == raw &&
                  guarded.getChannel(0)->getChannelId() == keptId,
              "the retry restored the same object with its id");
    }

    std::cout << (g_failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string(g_failures))
              << std::endl;
    return g_failures == 0 ? 0 : 1;
}
