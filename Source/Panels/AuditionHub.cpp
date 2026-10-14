// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AuditionHub.h"

#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../../AestraCore/include/AestraLog.h"
#include "Analysis/AudioAnalysis.h"
#include "IO/MiniAudioDecoder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <thread>

using namespace AestraUI;

namespace Aestra {

namespace {

uint64_t nowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string mmss(double seconds) {
    const int total = static_cast<int>(std::max(0.0, seconds));
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d:%02d", total / 60, total % 60);
    return buf;
}

NUIComponent* rootOf(NUIComponent* c) {
    while (c && c->getParent()) c = c->getParent();
    return c;
}

const char* pageName(AuditionHubLayout::Page p) {
    switch (p) {
        case AuditionHubLayout::Page::Home: return "Home";
        case AuditionHubLayout::Page::Project: return "This project";
        case AuditionHubLayout::Page::References: return "References";
        case AuditionHubLayout::Page::Queue: return "Files & queue";
        case AuditionHubLayout::Page::Search: return "Search";
    }
    return "";
}

/// The generated cover: a quiet background with two or three flat shapes, always the same for the same seed.
void drawCover(NUIRenderer& r, const NUIRect& rc, uint32_t seed) {
    struct Palette { uint32_t bg, a, b; };
    static const Palette palettes[] = {{0xff083d1d, 0xff3ec46d, 0xff18b8a6}, {0xff1d1536, 0xff14b8c9, 0xff7a6bd6}, {0xff0a2036, 0xfff08a00, 0xffb08a00},
                                       {0xff2a1230, 0xffe86bb9, 0xffb2506f}, {0xff301020, 0xfff0a0a0, 0xffc9a100}, {0xff5b1b1b, 0xffc9a100, 0xff6c87c9}};
    const Palette& p = palettes[seed % (sizeof(palettes) / sizeof(palettes[0]))];
    const NUIColor bg = NUIColor::fromHex(p.bg), a = NUIColor::fromHex(p.a), b = NUIColor::fromHex(p.b);
    r.fillRect(rc, bg);
    const float s = rc.width;
    switch ((seed >> 8) % 3) {
        case 0:  // a circle and a square overlapping
            r.fillCircle({rc.x + s * 0.40f, rc.y + s * 0.42f}, s * 0.26f, a);
            r.fillRect({rc.x + s * 0.50f, rc.y + s * 0.46f, s * 0.30f, s * 0.30f}, b.withAlpha(0.85f));
            break;
        case 1:  // two bars across
            r.fillRect({rc.x, rc.y + s * 0.62f, s, s * 0.13f}, a);
            r.fillRect({rc.x, rc.y + s * 0.75f, s, s * 0.06f}, b.withAlpha(0.8f));
            break;
        default:  // a tall bar and a ring
            r.fillRect({rc.x + s * 0.30f, rc.y, s * 0.10f, s}, a);
            r.strokeCircle({rc.x + s * 0.68f, rc.y + s * 0.42f}, s * 0.17f, 2.0f, b);
            break;
    }
}

} // namespace

AuditionHub::AuditionHub(std::shared_ptr<Audio::AuditionEngine> engine) : m_engine(std::move(engine)) {
    m_queuePage = std::make_shared<AuditionPanel>(m_engine);
    m_queuePage->setVisible(false);
    addChild(m_queuePage);
}

AuditionHub::~AuditionHub() {
    if (m_menu) {
        if (auto* parent = m_menu->getParent()) parent->removeChild(m_menu);
    }
}

void AuditionHub::setOnPlayRequest(std::function<void()> cb) {
    m_onPlayRequest = cb;
    m_queuePage->setOnPlayRequest(std::move(cb));
}
void AuditionHub::setOnActiveTrackPathChanged(std::function<void(const std::string&)> cb) { m_queuePage->setOnActiveTrackPathChanged(std::move(cb)); }
void AuditionHub::addFileToQueue(const std::string& filePath, bool isReference) { m_queuePage->addFileToQueue(filePath, isReference); }
void AuditionHub::addTimelineTrack(uint32_t trackId, const std::string& trackName) {
    m_queuePage->addTimelineTrack(trackId, trackName);
    setPage(Page::Queue);  // "Send to Audition" lands where the thing it sent is
}

void AuditionHub::setBrowser(std::shared_ptr<NUIComponent> browser) {
    releaseBrowser();
    m_browser = std::move(browser);
    if (!m_browser) return;
    addChild(m_browser);
    layoutQueuePage();
}

std::shared_ptr<NUIComponent> AuditionHub::releaseBrowser() {
    auto browser = std::move(m_browser);
    m_browser.reset();
    if (browser) removeChild(browser);
    return browser;
}

/// The Queue page: the file browser at the left, the queue it feeds beside it.
void AuditionHub::layoutQueuePage() {
    const NUIRect area = pageArea();
    constexpr float kBrowserW = 340.0f;
    const float browserW = m_browser ? std::min(kBrowserW, area.width * 0.45f) : 0.0f;
    if (m_browser) {
        m_browser->setBounds({area.x, area.y, browserW, area.height});
        m_browser->setVisible(m_page == Page::Queue);
    }
    m_queuePage->setBounds({area.x + browserW, area.y, area.width - browserW, area.height});
}

void AuditionHub::setLibraryFile(std::string file) {
    m_libraryFile = std::move(file);
    m_refList = m_libraryFile.empty() ? std::vector<AuditionLibrary::Reference>{} : AuditionLibrary::load(m_libraryFile);
    refresh();
}

void AuditionHub::refresh() {
    const Project project = m_projectProvider ? m_projectProvider() : Project{};
    m_projectName = project.name;
    m_versions = project.path.empty() ? std::vector<AuditionLibrary::Item>{}
                                      : AuditionLibrary::versionItems(project.name, project.path, TakeManager::loadManifest(project.path), nowMs());
    m_references.clear();
    for (const auto& r : m_refList) m_references.push_back(AuditionLibrary::itemOf(r));
    std::sort(m_references.begin(), m_references.end(), [](const auto& a, const auto& b) { return a.title < b.title; });
    setDirty(true);
}

void AuditionHub::addReference(const std::string& path) {
    if (path.empty() || !std::filesystem::exists(path)) return;
    for (const auto& r : m_refList) if (r.path == path) return;
    auto shared = m_shared;
    ++shared->measuring;
    std::thread([shared, path]() {
        AuditionLibrary::Reference ref;
        ref.path = path;
        ref.title = std::filesystem::path(path).stem().string();
        ref.addedMs = nowMs();
        std::vector<float> audio;
        uint32_t rate = 0, channels = 0;
        if (Audio::decodeAudioFile(path, audio, rate, channels) && channels > 0 && rate > 0) {
            ref.seconds = static_cast<double>(audio.size() / channels) / rate;
            const double lufs = Audio::analyzeAudio(audio, channels, rate).integratedLufs;
            if (lufs > -69.0) { ref.lufs = lufs; ref.hasLufs = true; }
        }
        std::lock_guard<std::mutex> lock(shared->mutex);
        shared->pending.push_back(std::move(ref));
        --shared->measuring;
    }).detach();
}

void AuditionHub::applyPendingReferences() {
    std::vector<AuditionLibrary::Reference> done;
    {
        std::lock_guard<std::mutex> lock(m_shared->mutex);
        done.swap(m_shared->pending);
    }
    if (done.empty()) return;
    for (auto& r : done) {
        const bool known = std::any_of(m_refList.begin(), m_refList.end(), [&](const auto& e) { return e.path == r.path; });
        if (!known) m_refList.push_back(std::move(r));
    }
    if (!m_libraryFile.empty() && !AuditionLibrary::save(m_libraryFile, m_refList)) Log::warning("[Audition] Could not save the reference list");
    refresh();
}

void AuditionHub::setPage(Page page) {
    if (page == m_page) return;
    m_page = page;
    m_scroll = 0.0f;
    m_queuePage->setVisible(page == Page::Queue);
    if (m_browser) m_browser->setVisible(page == Page::Queue);
    setFocused(page == Page::Search);
    if (page != Page::Search) m_search.clear();
    setDirty(true);
}

void AuditionHub::play(const AuditionLibrary::Item& item) {
    if (!m_engine || item.path.empty()) return;
    m_now = item;
    m_hasNow = true;
    if (m_onPlayRequest) m_onPlayRequest();
    const auto& queue = m_engine->getQueue();
    size_t index = queue.size();
    for (size_t i = 0; i < queue.size(); ++i) if (queue[i].filePath == item.path) { index = i; break; }
    if (index == queue.size()) {
        m_engine->addToQueue(item.path, item.kind == AuditionLibrary::Item::Kind::Reference);
        index = m_engine->getQueue().size() - 1;
    }
    m_engine->jumpToTrack(index);
    m_engine->play();
    setDirty(true);
}

// ---------------------------------------------------------------------------------------------------------------
// Model of the shelves, and their geometry
// ---------------------------------------------------------------------------------------------------------------

std::vector<AuditionLibrary::Item> AuditionHub::filtered(const std::vector<AuditionLibrary::Item>& items) const {
    if (m_search.empty()) return items;
    const auto lower = [](std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
    const std::string needle = lower(m_search);
    std::vector<AuditionLibrary::Item> out;
    for (const auto& i : items)
        if (lower(i.title + " " + i.subtitle + " " + i.badge).find(needle) != std::string::npos) out.push_back(i);
    return out;
}

std::vector<AuditionHub::ShelfModel> AuditionHub::shelfModels() const {
    std::vector<ShelfModel> out;
    const auto itemTiles = [](const std::vector<AuditionLibrary::Item>& items) {
        std::vector<Tile> tiles;
        for (const auto& i : items) tiles.push_back({Tile::Kind::Item, i});
        return tiles;
    };
    ShelfModel project{m_projectName.empty() ? "This project" : "This project", m_versions.empty() ? "" : "All " + std::to_string(m_versions.size()) + (m_versions.size() == 1 ? " version" : " versions"), {}};
    ShelfModel refs{"References", "Import...", {}};
    switch (m_page) {
        case Page::Home:
            project.tiles = itemTiles(m_versions);
            if (m_onBounceNow) project.tiles.push_back({Tile::Kind::BounceNow, {}});
            refs.tiles = itemTiles(m_references);
            out = {project, refs};
            break;
        case Page::Project:
            project.link.clear();
            project.tiles = itemTiles(m_versions);
            if (m_onBounceNow) project.tiles.push_back({Tile::Kind::BounceNow, {}});
            out = {project};
            break;
        case Page::References:
            refs.tiles = itemTiles(m_references);
            refs.tiles.push_back({Tile::Kind::Import, {}});
            refs.link.clear();
            out = {refs};
            break;
        case Page::Search: {
            ShelfModel found{"Results", "", {}};
            found.tiles = itemTiles(filtered(m_versions));
            for (auto& t : itemTiles(filtered(m_references))) found.tiles.push_back(std::move(t));
            out = {found};
            break;
        }
        case Page::Queue: break;
    }
    return out;
}

NUIRect AuditionHub::pageArea() const { return AuditionHubLayout::frame(getBounds()).content; }

float AuditionHub::shelvesTop() const { return m_page == Page::Home ? 112.0f : m_page == Page::Search ? 90.0f : 70.0f; }

std::vector<AuditionHubLayout::Shelf> AuditionHub::layoutShelves() const {
    const auto models = shelfModels();
    std::vector<size_t> counts;
    for (const auto& m : models) counts.push_back(m.tiles.size());
    const NUIRect content = pageArea();
    return AuditionHubLayout::shelves(content, content.y + shelvesTop() - m_scroll, counts);
}

bool AuditionHub::getNavCenter(Page page, NUIPoint& out) const {
    const auto f = AuditionHubLayout::frame(getBounds());
    const NUIRect& r = f.navItems[static_cast<size_t>(page)];
    out = {r.x + r.width * 0.5f, r.y + r.height * 0.5f};
    return true;
}

bool AuditionHub::getTileCenter(size_t shelf, size_t tile, NUIPoint& out) const {
    const auto shelves = layoutShelves();
    if (shelf >= shelves.size() || tile >= shelves[shelf].tiles.size()) return false;
    const NUIRect cover = shelves[shelf].cover(tile);
    out = {cover.x + cover.width * 0.5f, cover.y + cover.height * 0.5f};
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Frame loop
// ---------------------------------------------------------------------------------------------------------------

void AuditionHub::onResize(int width, int height) {
    NUIComponent::onResize(width, height);
    layoutQueuePage();
}

void AuditionHub::onUpdate(double deltaTime) {
    NUIComponent::onUpdate(deltaTime);
    if (!isVisible()) return;
    applyPendingReferences();
    if (++m_tick % 180 == 0) refresh();
    if (m_engine && m_engine->isPlaying()) setDirty(true);  // the position moves
    layoutQueuePage();
}

void AuditionHub::onRender(NUIRenderer& r) {
    if (!isVisible()) return;
    const auto f = AuditionHubLayout::frame(getBounds());
    auto& theme = NUIThemeManager::getInstance();
    r.fillRect(getBounds(), theme.getColor("backgroundPrimary"));
    renderNav(r, f);
    if (m_page == Page::Queue) renderChildren(r);
    else renderPage(r, f);
    renderPlayer(r, f);
    if (m_menu) m_menu->onRender(r);
}

void AuditionHub::renderNav(NUIRenderer& r, const AuditionHubLayout::Frame& f) {
    auto& theme = NUIThemeManager::getInstance();
    r.fillRect(f.nav, theme.getColor("backgroundSecondary"));
    r.fillRect({f.nav.right() - 1.0f, f.nav.y, 1.0f, f.nav.height}, theme.getColor("borderSubtle"));
    r.drawText("Audition", {f.nav.x + 24.0f, f.nav.y + 26.0f}, 20.0f, theme.getColor("textPrimary"));
    for (int i = 0; i < AuditionHubLayout::kPages; ++i) {
        const NUIRect& rect = f.navItems[static_cast<size_t>(i)];
        const bool active = static_cast<int>(m_page) == i;
        if (active) r.fillRect(rect, theme.getColor("surfaceRaised"));
        else if (m_hoverNav == i) r.fillRect(rect, theme.getColor("surfaceTertiary"));
        r.drawText(pageName(static_cast<Page>(i)), {rect.x + 20.0f, std::round(r.calculateTextY(rect, 13.0f))}, 13.0f,
                   active ? theme.getColor("textPrimary") : theme.getColor("textSecondary"));
        if (active) r.fillRect({rect.x, rect.y + 6.0f, 3.0f, rect.height - 12.0f}, theme.getColor("accentPrimary"));
    }
}

void AuditionHub::renderPage(NUIRenderer& r, const AuditionHubLayout::Frame& f) {
    auto& theme = NUIThemeManager::getInstance();
    const NUIColor text = theme.getColor("textPrimary"), muted = theme.getColor("textSecondary"), rule = theme.getColor("borderSubtle");
    const NUIRect content = f.content;
    r.setClipRect(content);

    // The heading, and a search box on the pages where you look for something.
    const std::string heading = m_page == Page::Home ? "Listen" : m_page == Page::Project ? (m_projectName.empty() ? "This project" : m_projectName)
                                : m_page == Page::References ? "References" : "Search";
    r.drawText(heading, {content.x + AuditionHubLayout::kPad, content.y + 26.0f - m_scroll}, 34.0f, text);
    if (m_page == Page::Home || m_page == Page::Search) {
        const NUIRect box{content.right() - AuditionHubLayout::kPad - 320.0f, content.y + 34.0f, 320.0f, 36.0f};
        r.fillRect(box, theme.getColor("surfaceTertiary"));
        if (m_page == Page::Search) r.strokeRect(box, 1.0f, theme.getColor("accentPrimary").withAlpha(0.7f));
        const std::string shown = m_search.empty() ? "Search bounces, references" : m_search + (m_page == Page::Search ? "|" : "");
        r.drawText(shown, {box.x + 14.0f, std::round(r.calculateTextY(box, 12.0f))}, 12.0f, m_search.empty() ? muted.withAlpha(0.7f) : text);
    }

    const auto models = shelfModels();
    const auto shelves = layoutShelves();
    const std::string nowPath = m_hasNow && m_engine && m_engine->isPlaying() ? m_now.path : std::string();
    for (size_t s = 0; s < models.size() && s < shelves.size(); ++s) {
        const auto& model = models[s];
        const auto& shelf = shelves[s];
        if (m_page == Page::Home) r.drawText(model.title, {shelf.header.x, shelf.header.y + 12.0f}, 20.0f, text);  // a page of one shelf is headed by the page
        if (!model.link.empty()) {
            const float lw = r.measureText(model.link, 11.0f).width;
            r.drawText(model.link, {shelf.header.right() - lw, shelf.header.y + 20.0f}, 11.0f, muted);
        }
        if (model.tiles.empty()) {
            const std::string none = model.title == "References" ? "Import a track you want to compare against." : "Nothing here yet.";
            r.drawText(none, {shelf.header.x, shelf.header.bottom() + 4.0f}, 12.0f, muted.withAlpha(0.8f));
        }
        for (size_t t = 0; t < model.tiles.size() && t < shelf.tiles.size(); ++t) {
            const Tile& tile = model.tiles[t];
            const NUIRect cover = shelf.cover(t);
            const bool hot = static_cast<int>(s) == m_hoverShelf && static_cast<int>(t) == m_hoverTile;
            if (tile.kind == Tile::Kind::Item) {
                drawCover(r, cover, tile.item.seed);
                if (hot) r.fillRect(cover, NUIColor::white().withAlpha(0.06f));
                const bool playing = !nowPath.empty() && tile.item.path == nowPath;
                if (playing) r.strokeRect(cover, 2.0f, theme.getColor("accentPrimary"));
                if (hot || playing) {  // the play affordance
                    const NUIPoint c{cover.right() - 30.0f, cover.bottom() - 30.0f};
                    r.fillCircle(c, 18.0f, theme.getColor("accentPrimary"));
                    for (int col = 0; col < 9; ++col) {
                        const float half = 7.0f * (1.0f - static_cast<float>(col) / 9.0f);
                        r.fillRect({c.x - 3.0f + static_cast<float>(col), c.y - half, 1.0f, half * 2.0f}, NUIColor::white());
                    }
                }
                r.drawText(tile.item.title, {cover.x, cover.bottom() + 10.0f}, 13.0f, text);
                float x = cover.x;
                if (!tile.item.badge.empty()) {
                    const bool ref = tile.item.kind == AuditionLibrary::Item::Kind::Reference;
                    const float bw = r.measureText(tile.item.badge, 9.5f).width + 12.0f;
                    r.fillRect({x, cover.bottom() + 30.0f, bw, 15.0f}, ref ? theme.getColor("warning") : theme.getColor("accentPrimary"));
                    r.drawText(tile.item.badge, {x + 6.0f, cover.bottom() + 32.0f}, 9.5f, ref ? NUIColor(0.1f, 0.07f, 0.02f, 1.0f) : NUIColor::white());
                    x += bw + 8.0f;
                }
                r.drawText(tile.item.subtitle, {x, cover.bottom() + 31.0f}, 11.0f, muted);
            } else {
                r.fillRect(cover, hot ? theme.getColor("surfaceTertiary") : NUIColor(0, 0, 0, 0));
                // A dashed outline: this is a place to put something, not something.
                const float dash = 6.0f, gap = 5.0f;
                for (float d = 0.0f; d < cover.width; d += dash + gap) {
                    const float len = std::min(dash, cover.width - d);
                    r.fillRect({cover.x + d, cover.y, len, 1.0f}, muted.withAlpha(0.6f));
                    r.fillRect({cover.x + d, cover.bottom() - 1.0f, len, 1.0f}, muted.withAlpha(0.6f));
                    r.fillRect({cover.x, cover.y + d, 1.0f, len}, muted.withAlpha(0.6f));
                    r.fillRect({cover.right() - 1.0f, cover.y + d, 1.0f, len}, muted.withAlpha(0.6f));
                }
                const std::string label = tile.kind == Tile::Kind::BounceNow ? "Bounce the timeline now" : "Import a reference...";
                const float lw = r.measureText(label, 12.0f).width;
                r.drawText(label, {cover.x + (cover.width - lw) * 0.5f, cover.bottom() - 26.0f}, 12.0f, text);
                const std::string sub = tile.kind == Tile::Kind::BounceNow ? "Adds v" + std::to_string(m_versions.size() + 1) + " to this project" : "Any audio file";
                r.drawText(sub, {cover.x, cover.bottom() + 12.0f}, 11.0f, muted);
            }
        }
    }
    if (m_shared->measuring.load() > 0) r.drawText("Measuring a reference...", {content.x + AuditionHubLayout::kPad, content.bottom() - 28.0f}, 11.5f, muted);
    (void)rule;
    r.clearClipRect();
}

void AuditionHub::renderPlayer(NUIRenderer& r, const AuditionHubLayout::Frame& f) {
    auto& theme = NUIThemeManager::getInstance();
    const NUIColor text = theme.getColor("textPrimary"), muted = theme.getColor("textSecondary"), rule = theme.getColor("borderSubtle");
    r.fillRect(f.player, theme.getColor("backgroundSecondary"));
    r.fillRect({f.player.x, f.player.y, f.player.width, 1.0f}, rule);

    // What is loaded: the item chosen here, else whatever the queue is playing.
    std::string title = "Nothing playing", sub, badge;
    uint32_t seed = 0;
    bool ref = false, hasLufs = false;
    double lufs = 0.0, fallbackSeconds = 0.0;
    if (m_hasNow) {
        title = m_now.title; sub = m_now.subtitle; badge = m_now.badge; seed = m_now.seed; ref = m_now.kind == AuditionLibrary::Item::Kind::Reference;
        hasLufs = m_now.hasLufs; lufs = m_now.lufs; fallbackSeconds = m_now.seconds;
    } else if (m_engine) {
        if (const auto item = m_engine->getCurrentItem()) { title = item->title.empty() ? std::filesystem::path(item->filePath).stem().string() : item->title; sub = item->artist; seed = AuditionLibrary::seedOf(title); fallbackSeconds = item->durationSeconds; }
    }
    drawCover(r, f.cover, seed);
    r.drawText(title, {f.cover.right() + 14.0f, f.cover.y + 8.0f}, 13.0f, text);
    float x = f.cover.right() + 14.0f;
    if (!badge.empty()) {
        const std::string label = ref ? badge : "MIX " + badge;
        const float bw = r.measureText(label, 9.5f).width + 12.0f;
        r.fillRect({x, f.cover.y + 30.0f, bw, 15.0f}, ref ? theme.getColor("warning") : theme.getColor("accentPrimary"));
        r.drawText(label, {x + 6.0f, f.cover.y + 32.0f}, 9.5f, ref ? NUIColor(0.1f, 0.07f, 0.02f, 1.0f) : NUIColor::white());
        x += bw + 8.0f;
    }
    r.setClipRect({x, f.player.y, std::max(0.0f, f.transportPrev.x - x - 20.0f), f.player.height});
    r.drawText(sub, {x, f.cover.y + 31.0f}, 11.0f, muted);
    r.clearClipRect();

    // Transport: previous, play or pause, next.
    const bool playing = m_engine && m_engine->isPlaying();
    const auto glyphBar = [&](const NUIRect& b, bool prev) {
        const NUIPoint c{b.x + b.width * 0.5f, b.y + b.height * 0.5f};
        const NUIColor col = m_hoverControl == (prev ? "prev" : "next") ? text : muted;
        r.fillRect({c.x + (prev ? -7.0f : 5.0f), c.y - 7.0f, 2.0f, 14.0f}, col);
        for (int i = 0; i < 10; ++i) {  // a triangle one column at a time, pointing away from the bar's side
            const float half = 7.0f * (prev ? static_cast<float>(i) / 10.0f : 1.0f - static_cast<float>(i) / 10.0f);
            r.fillRect({c.x + (prev ? -4.0f : -5.0f) + static_cast<float>(i), c.y - half, 1.0f, half * 2.0f}, col);
        }
    };
    glyphBar(f.transportPrev, true);
    glyphBar(f.transportNext, false);
    const NUIPoint pc{f.transportPlay.x + f.transportPlay.width * 0.5f, f.transportPlay.y + f.transportPlay.height * 0.5f};
    r.fillCircle(pc, 20.0f, m_hoverControl == "play" ? NUIColor::white() : NUIColor(0.93f, 0.91f, 0.87f, 1.0f));
    const NUIColor ink(0.05f, 0.04f, 0.03f, 1.0f);
    if (playing) {
        r.fillRect({pc.x - 6.0f, pc.y - 7.0f, 4.0f, 14.0f}, ink);
        r.fillRect({pc.x + 2.0f, pc.y - 7.0f, 4.0f, 14.0f}, ink);
    } else {
        for (int i = 0; i < 11; ++i) {
            const float half = 8.0f * (1.0f - static_cast<float>(i) / 11.0f);
            r.fillRect({pc.x - 4.0f + static_cast<float>(i), pc.y - half, 1.0f, half * 2.0f}, ink);
        }
    }

    // The seek bar, with the time at each end.
    const double duration = m_engine && m_engine->getDurationSeconds() > 0.0 ? m_engine->getDurationSeconds() : fallbackSeconds;
    const double pos = m_engine ? m_engine->getPositionSeconds() : 0.0;
    const float frac = duration > 0.0 ? static_cast<float>(std::clamp(pos / duration, 0.0, 1.0)) : 0.0f;
    const float barY = f.seek.y + f.seek.height * 0.5f;
    r.drawText(mmss(pos), {f.seek.x - 44.0f, barY - 6.0f}, 10.5f, muted);
    r.drawText(mmss(duration), {f.seek.right() + 10.0f, barY - 6.0f}, 10.5f, muted);
    r.fillRect({f.seek.x, barY - 1.5f, f.seek.width, 3.0f}, theme.getColor("surfaceRaised"));
    r.fillRect({f.seek.x, barY - 1.5f, f.seek.width * frac, 3.0f}, NUIColor(0.93f, 0.91f, 0.87f, 1.0f));

    // A (the mix) and B (the reference), the preset it is heard through, and how loud it is.
    const auto segment = [&](const NUIRect& b, const std::string& a, const std::string& label, bool on, bool enabled) {
        r.fillRect(b, on ? theme.getColor("accentPrimary") : theme.getColor("surfaceTertiary"));
        const std::string full = a + " \xc2\xb7 " + label;
        const float tw = r.measureText(full, 11.5f).width;
        r.drawText(full, {b.x + (b.width - tw) * 0.5f, std::round(r.calculateTextY(b, 11.5f))}, 11.5f,
                   on ? NUIColor::white() : (enabled ? muted : muted.withAlpha(0.45f)));
    };
    segment(f.toggleA, "A", "Mix", true, true);
    segment(f.toggleB, "B", "Ref", false, false);
    r.fillRect(f.listenOn, m_hoverControl == "listen" ? theme.getColor("surfaceRaised") : theme.getColor("surfaceTertiary"));
    const std::string preset = m_engine ? m_engine->getDSPPreset().name : "Studio";
    r.drawText(preset, {f.listenOn.x + 14.0f, std::round(r.calculateTextY(f.listenOn, 12.0f))}, 12.0f, text);
    const float cx = f.listenOn.right() - 16.0f, cy = f.listenOn.y + f.listenOn.height * 0.5f;
    r.drawLine({cx - 3.5f, cy - 1.5f}, {cx, cy + 2.0f}, 1.3f, muted);
    r.drawLine({cx, cy + 2.0f}, {cx + 3.5f, cy - 1.5f}, 1.3f, muted);
    if (hasLufs) {
        char buf[24];
        std::snprintf(buf, sizeof buf, "%.1f", lufs);
        r.drawText(buf, {f.loudness.x, f.loudness.y + 2.0f}, 12.0f, text);
        r.drawText("LUFS", {f.loudness.x + r.measureText(buf, 12.0f).width + 4.0f, f.loudness.y + 4.0f}, 10.0f, muted);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------------------------

void AuditionHub::openListenOnMenu(const NUIPoint& at) {
    if (!m_engine) return;
    if (m_menu) { if (auto* p = m_menu->getParent()) p->removeChild(m_menu); }
    m_menu = std::make_shared<NUIContextMenu>();
    const std::string current = m_engine->getDSPPreset().name;
    const auto add = [&](const std::string& label, const std::string& hint, Audio::AuditionDSPPreset preset) {
        auto item = std::make_shared<NUIContextMenuItem>(label);
        item->setShortcut(hint);
        item->setChecked(preset.name == current);
        item->setOnClick([this, preset]() { m_engine->setDSPPreset(preset); setDirty(true); });
        m_menu->addItem(item);
    };
    add("Studio", "Bypass. What you mixed.", Audio::AuditionDSPPreset::Bypass());
    add("Streaming", "-14 LUFS, limited", Audio::AuditionDSPPreset::Streaming());
    add("Lossless", "-16 LUFS, no limiter", Audio::AuditionDSPPreset::Lossless());
    add("Video platform", "-14 LUFS, rolled off", Audio::AuditionDSPPreset::VideoPlatform());
    add("Compressed stream", "-14 LUFS, low bitrate", Audio::AuditionDSPPreset::CompressedStream());
    add("Car", "bass up, treble down", Audio::AuditionDSPPreset::Car());
    add("Earbuds", "mild lift", Audio::AuditionDSPPreset::Earbuds());
    if (auto* root = rootOf(this)) {
        root->addChild(m_menu);
        m_menu->showAt(at);
        root->repaint();
    }
}

bool AuditionHub::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;

    if (m_menu) {  // a menu that is open gets the first word
        if (m_menu->onMouseEvent(event)) return true;
        if (event.pressed) {
            if (auto* p = m_menu->getParent()) p->removeChild(m_menu);
            m_menu.reset();
            setDirty(true);
        }
    }

    const auto f = AuditionHubLayout::frame(getBounds());
    const NUIPoint p = event.position;
    if (!getBounds().contains(p)) return false;

    if (event.type == NUIMouseEventType::Move) {
        m_hoverNav = -1;
        for (int i = 0; i < AuditionHubLayout::kPages; ++i) if (f.navItems[static_cast<size_t>(i)].contains(p)) m_hoverNav = i;
        m_hoverControl.clear();
        if (f.transportPrev.contains(p)) m_hoverControl = "prev";
        else if (f.transportNext.contains(p)) m_hoverControl = "next";
        else if (f.transportPlay.contains(p)) m_hoverControl = "play";
        else if (f.listenOn.contains(p)) m_hoverControl = "listen";
        m_hoverShelf = m_hoverTile = -1;
        if (m_page != Page::Queue && f.content.contains(p)) {
            const auto shelves = layoutShelves();
            for (size_t s = 0; s < shelves.size(); ++s)
                for (size_t t = 0; t < shelves[s].tiles.size(); ++t)
                    if (shelves[s].tiles[t].contains(p)) { m_hoverShelf = static_cast<int>(s); m_hoverTile = static_cast<int>(t); }
        }
        setDirty(true);
    }

    // The player bar and the navigation belong to the hub on every page.
    if (event.pressed && event.button == NUIMouseButton::Left) {
        for (int i = 0; i < AuditionHubLayout::kPages; ++i)
            if (f.navItems[static_cast<size_t>(i)].contains(p)) { setPage(static_cast<Page>(i)); return true; }
        if (f.player.contains(p)) {
            if (!m_engine) return true;
            if (f.transportPlay.contains(p)) { m_engine->togglePlayPause(); setDirty(true); }
            else if (f.transportPrev.contains(p)) m_engine->previousTrack();
            else if (f.transportNext.contains(p)) m_engine->nextTrack();
            else if (f.listenOn.contains(p)) openListenOnMenu({f.listenOn.x, f.listenOn.y - 8.0f - 7.0f * 28.0f});
            else if (NUIRect(f.seek.x, f.seek.y - 6.0f, f.seek.width, f.seek.height + 12.0f).contains(p))
                m_engine->seekNormalized(std::clamp((p.x - f.seek.x) / f.seek.width, 0.0f, 1.0f));
            return true;
        }
    }

    if (m_page == Page::Queue) return NUIComponent::onMouseEvent(event);

    if (event.type == NUIMouseEventType::Scroll && f.content.contains(p)) {
        const float top = f.content.y + shelvesTop();
        const float contentH = AuditionHubLayout::contentHeight(layoutShelves(), top - m_scroll) + shelvesTop();
        m_scroll = std::clamp(m_scroll - event.wheelDelta * 60.0f, 0.0f, std::max(0.0f, contentH - f.content.height));
        setDirty(true);
        return true;
    }

    if (event.pressed && event.button == NUIMouseButton::Left && f.content.contains(p)) {
        if ((m_page == Page::Home || m_page == Page::Search) &&
            NUIRect(f.content.right() - AuditionHubLayout::kPad - 320.0f, f.content.y + 34.0f, 320.0f, 36.0f).contains(p)) {
            setPage(Page::Search);
            return true;
        }
        const auto models = shelfModels();
        const auto shelves = layoutShelves();
        for (size_t s = 0; s < shelves.size() && s < models.size(); ++s) {
            if (models[s].link == "Import..." && NUIRect(shelves[s].header.right() - 90.0f, shelves[s].header.y, 90.0f, 40.0f).contains(p)) {
                if (m_onImportReference) m_onImportReference();
                return true;
            }
            for (size_t t = 0; t < shelves[s].tiles.size() && t < models[s].tiles.size(); ++t) {
                if (!shelves[s].tiles[t].contains(p)) continue;
                const Tile& tile = models[s].tiles[t];
                if (tile.kind == Tile::Kind::Item) play(tile.item);
                else if (tile.kind == Tile::Kind::BounceNow) { if (m_onBounceNow && m_onBounceNow()) refresh(); }
                else if (m_onImportReference) m_onImportReference();
                return true;
            }
        }
    }
    return true;  // the hub is solid: nothing under it should hear a click meant for it
}

bool AuditionHub::onKeyEvent(const NUIKeyEvent& event) {
    if (!isVisible()) return false;
    if (m_page == Page::Queue) return m_queuePage->onKeyEvent(event);
    if (m_page == Page::Search && event.pressed) {
        if (event.keyCode == NUIKeyCode::Escape) { m_search.clear(); setPage(Page::Home); return true; }
        if (event.keyCode == NUIKeyCode::Backspace) { if (!m_search.empty()) m_search.pop_back(); setDirty(true); return true; }
        if (event.character >= 32 && event.character < 127) { m_search.push_back(event.character); m_scroll = 0.0f; setDirty(true); return true; }
    }
    return false;
}

} // namespace Aestra
