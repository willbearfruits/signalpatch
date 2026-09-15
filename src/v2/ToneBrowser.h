#pragma once

#include "../audio/Tone3000.h"

#include <nanovg.h>

#include <atomic>
#include <functional>
#include <memory>
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

    void open (const juce::File& modelsFolder, std::function<void (const juce::File&)> onModelReady, std::function<void (const juce::String&)> say);
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
    void runAsync (std::function<juce::Result()> work, std::function<void (juce::Result)> done, bool evenIfClosed = false);
    void setStatus (juce::String text);
    [[nodiscard]] juce::Rectangle<float> panel (int windowWidth, int windowHeight) const noexcept;
    [[nodiscard]] int rowCount() const noexcept;
    [[nodiscard]] int rowAt (float x, float y) const noexcept;
    void pick (int row);
    void ensureVisible (int row) noexcept;

    NVGcontext* vg;
    int font;
    bool active = false;
    View view = View::login;
    juce::String query, status;
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

    static constexpr float rowHeight = 26.0f;
    static constexpr float headerHeight = 96.0f;
};
} // namespace signalpatch::v2
