// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// The File menu's import/export group, with the two MIDI file verbs. Kept out of AestraApp.cpp (size pin).
#include "AestraApp.h"

#include "Core/AestraContent.h"
#include "../AestraUI/Base/NUIContextMenu.h"

#include <filesystem>

void AestraApp::addImportExportMenu(AestraUI::NUIContextMenu& menu) {
    menu.addItem("Import MIDI File...", [this]() {
        const std::string filter = std::string("MIDI File\0*.mid;*.midi\0All Files\0*.*\0",
                                               sizeof("MIDI File\0*.mid;*.midi\0All Files\0*.*\0") - 1);
        pickFileAsync([filter](Aestra::IPlatformUtils& utils) { return utils.openFileDialog("Import MIDI File", filter); },
                      [this](const std::string& pickedPath) {
                          if (!pickedPath.empty() && m_content) m_content->importMidiFile(pickedPath);
                      });
    });
    menu.addItem("Export Audio...", [this]() { startExport(); });
    menu.addItem("Export Selected Pattern as MIDI...", [this]() {
        if (!m_content) return;
        Aestra::IPlatformUtils::SaveFileDialogOptions options;
        options.title = "Export Selected Pattern as MIDI";
        options.filter = std::string("MIDI File\0*.mid\0All Files\0*.*\0", sizeof("MIDI File\0*.mid\0All Files\0*.*\0") - 1);
        options.defaultExtension = "mid";
        pickFileAsync([options](Aestra::IPlatformUtils& utils) { return utils.saveFileDialog(options); },
                      [this](const std::string& pickedPath) {
                          if (!pickedPath.empty() && m_content) m_content->exportActivePatternAsMidi(pickedPath);
                      });
    });
}
