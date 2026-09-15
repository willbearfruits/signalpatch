#pragma once

#include "../audio/Tone3000.h"

#include <nanovg.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace signalpatch::v2
{
// In-canvas TONE3000 browser: log in once (the system browser does the
// OAuth dance and comes back to a localhost listener), type to search the
// captures, Enter opens a tone's models, Enter again downloads the .nam
// into the models folder and hands the file back. Network work runs on
// one worker thread; results hop to the message thread, which the frame
// loop pumps.
class ToneBrowser
{
public:
    ToneBrowser (NVGcontext* vg, int font);
    ~ToneBrowser();

    enum class Mode { captures, impulses };
    void open (const juce::File& modelsFolder, std::function<void (const juce::File&)> onModelReady, std::function<void (const juce::String&)> say,
               Mode mode = Mode::captures);
    [[nodiscard]] bool isOpen() const noexcept { return active; }
    void close();

    bool mouseMove (float x, float y);
    bool mouseButton (int button, bool pressed, float x, float y, double now);
    bool scroll (double dy);
    bool key (int key, int mods);
    bool character (juce::juce_wchar codepoint);
    void draw (int windowWidth, int windowHeight, double now);
    /** True once after anything changed off the input path (a result arrived). */
    bool consumeDirty() noexcept { return dirty.exchange (false); }

private:
    enum class View { login, waitingForBrowser, tones, models };

    void beginLogin();
    void finishLogin (const juce::String& code);
    void search (int page);
    void openTone (const tone3000::Tone& tone);
    void download (const tone3000::Model& model);
    /** work runs on the pool with its own copy of the client; results go into objects the lambdas share,
        and the message thread adopts them (and the possibly refreshed tokens) after the hop. */
    void runAsync (std::function<juce::Result (tone3000::Client&)> work, std::function<void (juce::Result)> done, bool evenIfClosed = false);
    void setStatus (juce::String text);
    [[nodiscard]] juce::Rectangle<float> panel (int windowWidth, int windowHeight) const noexcept;
    [[nodiscard]] int rowCount() const noexcept;
    [[nodiscard]] int rowAt (float x, float y) const noexcept;
    void pick (int row);
    void ensureVisible (int row) noexcept;
    void requestImage (const tone3000::Tone& tone);
    [[nodiscard]] juce::StringArray gearChoices() const;
    [[nodiscard]] juce::Rectangle<float> chipBounds (int row, int index, int count) const noexcept;
    struct Chip { int row = -1, index = -1; };
    [[nodiscard]] Chip chipAt (float x, float y) const noexcept;

    NVGcontext* vg;
    int font;
    bool active = false;
    View view = View::login;
    juce::String query, status;
    Mode mode = Mode::captures;
    tone3000::SearchOptions options;
    int gearIndex = 0, sortIndex = 0;
    std::map<juce::int64, int> images;   // tone id -> NanoVG image (0 = failed)
    std::set<juce::int64> imagesPending;
    tone3000::TonePage page;
    std::vector<tone3000::Model> models;
    tone3000::Tone currentTone;
    int selected = -1, hover = -1;
    float scrollOffset = 0.0f;
    bool busy = false;
    juce::File folder;
    std::function<void (const juce::File&)> modelReady;
    std::function<void (const juce::String&)> announce;
    juce::Rectangle<float> lastPanel;
    double lastClickTime = -1.0;
    int lastClickRow = -1;
    std::atomic<bool> dirty { false };

    tone3000::Client client { tone3000::loadPublishableKey() };
    tone3000::CallbackServer server;
    tone3000::Pkce pkce;
    juce::ThreadPool pool { 1 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    int generation = 0;

    static constexpr float rowHeight = 34.0f;
    static constexpr float headerHeight = 124.0f;
    static constexpr float thumbWidth = 46.0f;
};
} // namespace signalpatch::v2
