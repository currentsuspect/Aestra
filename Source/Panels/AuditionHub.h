// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

/**
 * @file AuditionHub.h
 * @brief The Audition mode's shell (FD-26, V81-A): navigation at the left, a player bar along the bottom, and pages
 * of tiles between them: the project's bounces and the reference tracks. The queue that used to be the whole of
 * Audition is one page of the hub (AuditionPanel), with everything it did.
 */

#include "../AestraUI/Core/NUIComponent.h"
#include "../Core/AuditionLibrary.h"
#include "AuditionEngine.h"
#include "AuditionHubLayout.h"
#include "AuditionPanel.h"
#include "NUIContextMenu.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Aestra {

class AuditionHub : public AestraUI::NUIComponent {
public:
    using Page = AuditionHubLayout::Page;

    explicit AuditionHub(std::shared_ptr<Audio::AuditionEngine> engine);
    ~AuditionHub() override;

    void onRender(AestraUI::NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const AestraUI::NUIMouseEvent& event) override;
    bool onKeyEvent(const AestraUI::NUIKeyEvent& event) override;

    // What the queue page always did; the app's existing wiring keeps working through these.
    void setOnPlayRequest(std::function<void()> cb);
    void setOnActiveTrackPathChanged(std::function<void(const std::string&)> cb);
    void addFileToQueue(const std::string& filePath, bool isReference = false);
    void addTimelineTrack(uint32_t trackId, const std::string& trackName);

    struct Project {
        std::string name;
        std::string path;
    };
    void setProjectProvider(std::function<Project()> provider) { m_projectProvider = std::move(provider); }
    void setLibraryFile(std::string file);
    /// The user asked to import a reference: the app picks a file and calls addReference.
    void setOnImportReference(std::function<void()> cb) { m_onImportReference = std::move(cb); }
    /// The user asked to bounce the timeline now; true when it was bounced.
    void setOnBounceNow(std::function<bool()> cb) { m_onBounceNow = std::move(cb); }
    void addReference(const std::string& path);

    /// Re-read the project's bounces and the reference list (the hub does this when it is shown, and every few seconds).
    void refresh();
    void setPage(Page page);
    Page getPage() const { return m_page; }
    const std::vector<AuditionLibrary::Item>& versions() const { return m_versions; }
    const std::vector<AuditionLibrary::Item>& references() const { return m_references; }
    void play(const AuditionLibrary::Item& item);

    // For tests and hit-testing.
    bool getNavCenter(Page page, AestraUI::NUIPoint& out) const;
    bool getTileCenter(size_t shelf, size_t tile, AestraUI::NUIPoint& out) const;

private:
    struct Tile {
        enum class Kind { Item, BounceNow, Import } kind = Kind::Item;
        AuditionLibrary::Item item;
    };
    struct ShelfModel {
        std::string title;
        std::string link;   ///< the quiet text at the right of the header ("Import...")
        std::vector<Tile> tiles;
    };
    std::vector<ShelfModel> shelfModels() const;
    std::vector<AuditionLibrary::Item> filtered(const std::vector<AuditionLibrary::Item>& items) const;
    AestraUI::NUIRect pageArea() const;
    float shelvesTop() const;

    void renderNav(AestraUI::NUIRenderer& r, const AuditionHubLayout::Frame& f);
    void renderPage(AestraUI::NUIRenderer& r, const AuditionHubLayout::Frame& f);
    void renderPlayer(AestraUI::NUIRenderer& r, const AuditionHubLayout::Frame& f);
    void openListenOnMenu(const AestraUI::NUIPoint& at);
    void applyPendingReferences();
    std::vector<AuditionHubLayout::Shelf> layoutShelves() const;
    void layoutPlayerTexts();

    std::shared_ptr<Audio::AuditionEngine> m_engine;
    std::shared_ptr<AuditionPanel> m_queuePage;
    Page m_page{Page::Home};
    std::string m_search;
    float m_scroll{0.0f};
    int m_hoverNav{-1};
    int m_hoverShelf{-1}, m_hoverTile{-1};
    std::string m_hoverControl;
    unsigned m_tick{0};

    std::function<Project()> m_projectProvider;
    std::function<void()> m_onImportReference;
    std::function<bool()> m_onBounceNow;
    std::function<void()> m_onPlayRequest;
    std::string m_libraryFile;
    std::vector<AuditionLibrary::Item> m_versions, m_references;
    std::string m_projectName;

    AuditionLibrary::Item m_now;
    bool m_hasNow{false};

    // References are measured off the UI thread; the results wait here until the UI thread applies them. Shared with
    // the worker so a hub that is gone before the measuring is done is not touched.
    struct Shared {
        std::mutex mutex;
        std::vector<AuditionLibrary::Reference> pending;
        std::atomic<int> measuring{0};
    };
    std::shared_ptr<Shared> m_shared = std::make_shared<Shared>();
    std::vector<AuditionLibrary::Reference> m_refList;

    std::shared_ptr<AestraUI::NUIContextMenu> m_menu;
};

} // namespace Aestra
