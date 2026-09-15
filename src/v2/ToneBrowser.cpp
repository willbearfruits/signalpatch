#include "ToneBrowser.h"
#include "Palette.h"

#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace signalpatch::v2
{
ToneBrowser::ToneBrowser (NVGcontext* context, int fontId) : vg (context), font (fontId)
{
    client.tokens = tone3000::loadTokens();
}

ToneBrowser::~ToneBrowser()
{
    *alive = false;
    server.stop();
    pool.removeAllJobs (true, 4000);
    for (const auto& [id, image] : images)
        if (image > 0)
            nvgDeleteImage (vg, image);
}

namespace
{
    const juce::StringArray sortLabels { "TRENDING", "NEWEST", "MOST DOWNLOADED", "OLDEST" };
    const juce::StringArray sortValues { "trending", "newest", "downloads-all-time", "oldest" };
}

juce::StringArray ToneBrowser::gearChoices() const
{
    return mode == Mode::impulses ? juce::StringArray { "ALL", "CAB", "SPACE" }
                                  : juce::StringArray { "ALL", "AMP", "AMP+CAB", "PEDAL", "OUTBOARD" };
}

void ToneBrowser::open (const juce::File& modelsFolder, std::function<void (const juce::File&)> onModelReady, std::function<void (const juce::String&)> say, Mode newMode)
{
    folder = modelsFolder;
    modelReady = std::move (onModelReady);
    announce = std::move (say);
    active = true;
    selected = -1;
    hover = -1;
    scrollOffset = 0.0f;
    const bool modeChanged = mode != newMode;
    mode = newMode;
    options.format = mode == Mode::impulses ? "ir" : "nam";
    if (modeChanged)
    {
        gearIndex = 0;
        options.gear.clear();
        page = {};
        models.clear();
    }
    if (client.tokens.present())
    {
        view = View::tones;
        if (page.tones.empty())
            search (1);
        else
            setStatus (juce::String (page.total) + " tones");
    }
    else
    {
        view = View::login;
        setStatus (tone3000::loadPublishableKey().isEmpty()
                       ? "No TONE3000 key: put your publishable key in " + tone3000::keysFile().getFullPathName()
                       : "Press Enter to log in with TONE3000 (opens your browser once)");
    }
    dirty = true;
}

void ToneBrowser::close()
{
    active = false;
    ++generation; // late results are ignored
    server.stop();
}

void ToneBrowser::setStatus (juce::String text)
{
    status = std::move (text);
    dirty = true;
}

void ToneBrowser::runAsync (std::function<juce::Result (tone3000::Client&)> work, std::function<void (juce::Result)> done, bool evenIfClosed)
{
    busy = true;
    dirty = true;
    const auto expected = ++generation;
    auto keepAlive = alive;
    auto worker = std::make_shared<tone3000::Client> (client); // the pool never touches members
    pool.addJob (std::function<juce::ThreadPoolJob::JobStatus()> ([this, keepAlive, expected, evenIfClosed, worker, work = std::move (work), done = std::move (done)]
    {
        auto result = work (*worker);
        juce::MessageManager::callAsync ([this, keepAlive, expected, evenIfClosed, worker, result, done]
        {
            if (! *keepAlive)
                return;
            busy = false;
            dirty = true;
            // Refresh tokens rotate: keep whatever the worker ended with, even for a stale request.
            if (worker->tokens.present() && (worker->tokens.access != client.tokens.access || worker->tokens.refresh != client.tokens.refresh))
            {
                client.tokens = worker->tokens;
                tone3000::saveTokens (client.tokens);
            }
            if (expected == generation || evenIfClosed)
                done (result);
        });
        return juce::ThreadPoolJob::jobHasFinished;
    }));
}

void ToneBrowser::beginLogin()
{
    const auto key = tone3000::loadPublishableKey();
    if (key.isEmpty())
    {
        setStatus ("No TONE3000 key in " + tone3000::keysFile().getFullPathName());
        return;
    }
    pkce = tone3000::Pkce::generate();
    const auto expectedState = pkce.state;
    auto keepAlive = alive;
    if (! server.start (expectedState, [this, keepAlive] (juce::String code)
        {
            juce::MessageManager::callAsync ([this, keepAlive, code] { if (*keepAlive) finishLogin (code); });
        }))
    {
        setStatus ("Could not listen on localhost:" + juce::String (tone3000::callbackPort) + " for the login");
        return;
    }
    view = View::waitingForBrowser;
    setStatus ("Waiting for the browser... log in and allow SignalPatch (Esc cancels)");
    tone3000::authorizeUrl (key, tone3000::redirectUri(), pkce).launchInDefaultBrowser();
}

void ToneBrowser::finishLogin (const juce::String& code)
{
    server.stop();
    setStatus ("Exchanging the code...");
    const auto verifier = pkce.verifier;
    runAsync ([code, verifier] (tone3000::Client& worker) { return worker.exchangeCode (code, verifier, tone3000::redirectUri()); },
              [this] (juce::Result result)
    {
        if (result.failed())
        {
            view = View::login;
            setStatus (result.getErrorMessage() + " - Enter to try again");
            if (announce) announce ("TONE3000: " + result.getErrorMessage());
            return;
        }
        view = View::tones; // tokens were adopted and saved in the hop, even if the panel closed meanwhile
        if (announce) announce ("Logged in to TONE3000");
        if (active)
            search (1);
    }, true);
}

void ToneBrowser::search (int pageNumber)
{
    setStatus ("Searching...");
    const auto text = query;
    auto wanted = options;
    wanted.sort = text.trim().isNotEmpty() && sortIndex == 0 ? juce::String() : sortValues[sortIndex]; // a query sorts by match unless asked
    auto found = std::make_shared<tone3000::TonePage>();
    runAsync ([text, pageNumber, wanted, found] (tone3000::Client& worker)
    {
        if (auto refreshed = worker.refreshIfNeeded(); refreshed.failed())
            return refreshed;
        return worker.search (text, pageNumber, wanted, *found);
    },
    [this, found] (juce::Result result)
    {
        if (result.failed())
        {
            if (result.getErrorMessage().contains ("log in again") || result.getErrorMessage().contains ("Not logged in"))
            {
                client.tokens = {};
                tone3000::forgetTokens();
                view = View::login;
            }
            setStatus (result.getErrorMessage());
            return;
        }
        page = std::move (*found); // adopted on the message thread only
        view = View::tones;
        selected = page.tones.empty() ? -1 : 0;
        scrollOffset = 0.0f;
        setStatus (page.tones.empty() ? juce::String ("Nothing found")
                                      : juce::String (page.total) + (mode == Mode::impulses ? " impulse packs, page " : " tones, page ")
                                        + juce::String (page.page) + "/" + juce::String (page.totalPages) + "   (Left / Right: pages)");
    });
}

void ToneBrowser::openTone (const tone3000::Tone& tone)
{
    currentTone = tone;
    setStatus ("Loading models of " + tone.title + "...");
    const auto id = tone.id;
    auto found = std::make_shared<std::vector<tone3000::Model>>();
    runAsync ([id, found] (tone3000::Client& worker)
    {
        if (auto refreshed = worker.refreshIfNeeded(); refreshed.failed())
            return refreshed;
        return worker.models (id, *found);
    },
    [this, found] (juce::Result result)
    {
        if (result.failed())
        {
            setStatus (result.getErrorMessage());
            return;
        }
        models = std::move (*found);
        view = View::models;
        selected = models.empty() ? -1 : 0;
        scrollOffset = 0.0f;
        setStatus (models.empty() ? (mode == Mode::impulses ? "This pack has no downloadable impulses" : "This tone has no downloadable NAM models")
                                  : juce::String (models.size()) + (mode == Mode::impulses ? " impulses - Enter loads one into the cabinet, keep trying others   (Esc back)"
                                                                                           : " models - Enter loads one and you play it, try the next   (Esc back)"));
    });
}

void ToneBrowser::download (const tone3000::Model& model)
{
    const auto destination = folder.getChildFile (tone3000::modelFileName (currentTone, model, mode == Mode::impulses ? "wav" : "nam"));
    if (destination.existsAsFile())
    {
        if (modelReady) modelReady (destination);
        setStatus ("Loaded " + destination.getFileName() + " - play it; Enter on another to compare, Esc when happy");
        return;
    }
    setStatus ("Downloading " + model.name + "...");
    runAsync ([model, destination] (tone3000::Client& worker)
    {
        if (auto refreshed = worker.refreshIfNeeded(); refreshed.failed())
            return refreshed;
        return worker.download (model, destination);
    },
    [this, destination] (juce::Result result)
    {
        if (result.failed())
        {
            setStatus (result.getErrorMessage());
            return;
        }
        if (modelReady) modelReady (destination);
        setStatus ("Loaded " + destination.getFileName() + " - play it; Enter on another to compare, Esc when happy");
    });
}

void ToneBrowser::requestImage (const tone3000::Tone& tone)
{
    if (tone.image.isEmpty() || images.count (tone.id) != 0 || imagesPending.count (tone.id) != 0 || images.size() > 400)
        return;
    imagesPending.insert (tone.id);
    auto keepAlive = alive;
    const auto id = tone.id;
    const auto url = tone.image;
    pool.addJob (std::function<juce::ThreadPoolJob::JobStatus()> ([this, keepAlive, id, url]
    {
        auto bytes = std::make_shared<juce::MemoryBlock>();
        const auto result = tone3000::Client::fetchBytes (url, *bytes);
        juce::MessageManager::callAsync ([this, keepAlive, id, bytes, ok = result.wasOk()]
        {
            if (! *keepAlive)
                return;
            imagesPending.erase (id);
            int handle = 0;
            if (ok && bytes->getSize() > 0)
                handle = nvgCreateImageMem (vg, 0, static_cast<unsigned char*> (bytes->getData()), static_cast<int> (bytes->getSize()));
            images[id] = handle; // 0 remembers the failure so it is not fetched again
            dirty = true;
        });
        return juce::ThreadPoolJob::jobHasFinished;
    }));
}

juce::Rectangle<float> ToneBrowser::chipBounds (int row, int index, int count) const noexcept
{
    const auto x = lastPanel.getX() + 14.0f;
    const auto width = (lastPanel.getWidth() - 28.0f - 4.0f * static_cast<float> (count - 1)) / static_cast<float> (juce::jmax (1, count));
    return { x + static_cast<float> (index) * (width + 4.0f), lastPanel.getY() + 72.0f + static_cast<float> (row) * 22.0f, width, 18.0f };
}

ToneBrowser::Chip ToneBrowser::chipAt (float x, float y) const noexcept
{
    const auto gears = gearChoices();
    for (int i = 0; i < gears.size(); ++i)
        if (chipBounds (0, i, gears.size()).contains (x, y))
            return { 0, i };
    for (int i = 0; i < sortLabels.size(); ++i)
        if (chipBounds (1, i, sortLabels.size()).contains (x, y))
            return { 1, i };
    return {};
}

int ToneBrowser::rowCount() const noexcept
{
    return view == View::tones ? static_cast<int> (page.tones.size()) : view == View::models ? static_cast<int> (models.size()) : 0;
}

void ToneBrowser::pick (int row)
{
    if (busy || row < 0 || row >= rowCount())
        return;
    if (view == View::tones)
        openTone (page.tones[static_cast<std::size_t> (row)]);
    else if (view == View::models)
        download (models[static_cast<std::size_t> (row)]);
}

void ToneBrowser::ensureVisible (int row) noexcept
{
    const auto rowTop = static_cast<float> (row) * rowHeight;
    const auto listHeight = lastPanel.getHeight() - headerHeight - 30.0f;
    if (rowTop - scrollOffset > listHeight - rowHeight)
        scrollOffset = rowTop - listHeight + rowHeight;
    if (rowTop < scrollOffset)
        scrollOffset = rowTop;
}

bool ToneBrowser::key (int keyCode, int mods)
{
    if (! active)
        return false;
    juce::ignoreUnused (mods);
    if (keyCode == GLFW_KEY_ESCAPE)
    {
        if (view == View::models)
        {
            view = View::tones;
            selected = -1;
            scrollOffset = 0.0f;
            setStatus (juce::String (page.total) + " tones, page " + juce::String (page.page) + "/" + juce::String (page.totalPages));
        }
        else if (view == View::waitingForBrowser)
        {
            server.stop();
            view = View::login;
            setStatus ("Login cancelled - Enter to try again");
        }
        else
            close();
    }
    else if (keyCode == GLFW_KEY_ENTER || keyCode == GLFW_KEY_KP_ENTER)
    {
        if (view == View::login)
            beginLogin();
        else if (view == View::tones && selected < 0)
            search (1);
        else
            pick (selected);
    }
    else if (keyCode == GLFW_KEY_BACKSPACE)
    {
        if (view == View::tones && query.isNotEmpty())
        {
            query = query.dropLastCharacters (1);
            selected = -1;
        }
    }
    else if (keyCode == GLFW_KEY_DOWN || keyCode == GLFW_KEY_UP)
    {
        const auto count = rowCount();
        if (count > 0)
        {
            selected = juce::jlimit (0, count - 1, selected + (keyCode == GLFW_KEY_DOWN ? 1 : -1));
            ensureVisible (selected);
        }
    }
    else if (keyCode == GLFW_KEY_TAB && view == View::tones && ! busy)
    {
        const auto gears = gearChoices();
        if ((mods & GLFW_MOD_SHIFT) != 0)
            sortIndex = (sortIndex + 1) % sortLabels.size();
        else
        {
            gearIndex = (gearIndex + 1) % gears.size();
            options.gear = gearIndex == 0 ? juce::String() : gears[gearIndex].toLowerCase().replace ("+", "-");
        }
        search (1);
    }
    else if ((keyCode == GLFW_KEY_RIGHT || keyCode == GLFW_KEY_LEFT) && view == View::tones && ! busy)
    {
        const auto next = page.page + (keyCode == GLFW_KEY_RIGHT ? 1 : -1);
        if (next >= 1 && next <= page.totalPages)
            search (next);
    }
    dirty = true;
    return true;
}

bool ToneBrowser::character (juce::juce_wchar codepoint)
{
    if (! active || view != View::tones)
        return active;
    if (codepoint >= 32 && query.length() < 60)
    {
        query += juce::String::charToString (codepoint);
        selected = -1;
        dirty = true;
    }
    return true;
}

juce::Rectangle<float> ToneBrowser::panel (int windowWidth, int windowHeight) const noexcept
{
    const auto w = juce::jmin (720.0f, static_cast<float> (windowWidth) - 40.0f);
    const auto h = juce::jmin (600.0f, static_cast<float> (windowHeight) - 80.0f);
    return { (windowWidth - w) * 0.5f, (windowHeight - h) * 0.45f, w, h };
}

int ToneBrowser::rowAt (float x, float y) const noexcept
{
    const auto listTop = lastPanel.getY() + headerHeight;
    if (x < lastPanel.getX() || x > lastPanel.getRight() || y < listTop || y > lastPanel.getBottom() - 30.0f)
        return -1;
    const auto row = static_cast<int> ((y - listTop + scrollOffset) / rowHeight);
    return row >= 0 && row < rowCount() ? row : -1;
}

bool ToneBrowser::mouseMove (float x, float y)
{
    if (! active)
        return false;
    const auto row = rowAt (x, y);
    if (row != hover)
    {
        hover = row;
        dirty = true;
    }
    return true;
}

bool ToneBrowser::mouseButton (int button, bool pressed, float x, float y, double now)
{
    if (! active)
        return false;
    if (! pressed || button != GLFW_MOUSE_BUTTON_LEFT)
        return true;
    if (! lastPanel.contains (x, y))
    {
        close();
        return true;
    }
    const auto row = rowAt (x, y);
    if (row < 0)
    {
        if (view == View::login && y < lastPanel.getY() + headerHeight)
            beginLogin();
        else if (view == View::tones && ! busy)
        {
            const auto chip = chipAt (x, y);
            if (chip.row == 0)
            {
                gearIndex = chip.index;
                const auto gears = gearChoices();
                options.gear = gearIndex == 0 ? juce::String() : gears[gearIndex].toLowerCase().replace ("+", "-");
                search (1);
            }
            else if (chip.row == 1)
            {
                sortIndex = chip.index;
                search (1);
            }
        }
        return true;
    }
    const bool doubleClick = row == lastClickRow && now - lastClickTime < 0.4;
    lastClickRow = row;
    lastClickTime = now;
    selected = row;
    if (doubleClick)
        pick (row);
    dirty = true;
    return true;
}

bool ToneBrowser::scroll (double dy)
{
    if (! active)
        return false;
    const auto contentHeight = static_cast<float> (rowCount()) * rowHeight;
    const auto listHeight = lastPanel.getHeight() - headerHeight - 30.0f;
    scrollOffset = juce::jlimit (0.0f, juce::jmax (0.0f, contentHeight - listHeight), scrollOffset - static_cast<float> (dy) * rowHeight * 2.0f);
    dirty = true;
    return true;
}

void ToneBrowser::draw (int windowWidth, int windowHeight, double now)
{
    if (! active)
        return;
    lastPanel = panel (windowWidth, windowHeight);
    const auto x = lastPanel.getX(), y = lastPanel.getY(), w = lastPanel.getWidth(), h = lastPanel.getHeight();
    nvgBeginPath (vg);
    nvgRect (vg, 0, 0, static_cast<float> (windowWidth), static_cast<float> (windowHeight));
    nvgFillColor (vg, nvgRGBAf (0, 0, 0, 0.35f));
    nvgFill (vg);
    nvgBeginPath (vg);
    nvgRoundedRect (vg, x, y, w, h, 8.0f);
    nvgFillColor (vg, palette::panelRaised);
    nvgFill (vg);
    nvgStrokeColor (vg, nvgRGBAf (1, 1, 1, 0.1f));
    nvgStroke (vg);

    nvgFontFaceId (vg, font);
    nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFontSize (vg, 11.0f);
    nvgTextLetterSpacing (vg, 0.8f);
    nvgFillColor (vg, palette::mutedText);
    const auto title = (mode == Mode::impulses ? juce::String ("TONE3000 IMPULSES") : juce::String ("TONE3000"))
                     + (view == View::models ? "  /  " + currentTone.title.toUpperCase() : juce::String());
    nvgText (vg, x + 16.0f, y + 20.0f, title.toRawUTF8(), nullptr);
    nvgTextLetterSpacing (vg, 0.0f);

    // Search box (tones) or the login prompt.
    nvgBeginPath (vg);
    nvgRoundedRect (vg, x + 14.0f, y + 36.0f, w - 28.0f, 30.0f, 4.0f);
    nvgFillColor (vg, palette::nodeDark);
    nvgFill (vg);
    nvgFontSize (vg, 14.0f);
    if (view == View::tones)
    {
        nvgFillColor (vg, query.isEmpty() ? alpha (palette::mutedText, 0.6f) : palette::text);
        const auto shown = query.isEmpty() ? juce::String ("search captures (Enter for trending)") : query;
        nvgText (vg, x + 24.0f, y + 51.0f, shown.toRawUTF8(), nullptr);
        if (! query.isEmpty() || std::fmod (now, 1.0) < 0.55)
        {
            float bounds[4] {};
            nvgTextBounds (vg, x + 24.0f, y + 51.0f, query.toRawUTF8(), nullptr, bounds);
            if (std::fmod (now, 1.0) < 0.55)
            {
                nvgBeginPath (vg);
                nvgRect (vg, (query.isEmpty() ? x + 24.0f : bounds[2] + 1.0f), y + 43.0f, 1.5f, 16.0f);
                nvgFillColor (vg, palette::selection);
                nvgFill (vg);
            }
        }
    }
    else
    {
        nvgFillColor (vg, palette::text);
        nvgText (vg, x + 24.0f, y + 51.0f, view == View::models ? currentTone.title.toRawUTF8()
                                              : view == View::login ? "Log in with TONE3000  (Enter)" : "Finish the login in your browser...", nullptr);
    }
    if (view == View::tones)
    {
        const auto gears = gearChoices();
        auto drawChips = [&] (int row, const juce::StringArray& labels, int current)
        {
            for (int i = 0; i < labels.size(); ++i)
            {
                const auto chip = chipBounds (row, i, labels.size());
                nvgBeginPath (vg);
                nvgRoundedRect (vg, chip.getX(), chip.getY(), chip.getWidth(), chip.getHeight(), 9.0f);
                nvgFillColor (vg, i == current ? alpha (palette::control, 0.85f) : nvgRGBAf (1, 1, 1, 0.06f));
                nvgFill (vg);
                nvgFontSize (vg, 8.5f);
                nvgTextAlign (vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                nvgFillColor (vg, i == current ? palette::nodeDark : alpha (palette::mutedText, 0.9f));
                nvgText (vg, chip.getCentreX(), chip.getCentreY(), labels[i].toRawUTF8(), nullptr);
            }
        };
        drawChips (0, gears, gearIndex);
        drawChips (1, sortLabels, sortIndex);
    }
    nvgFontSize (vg, 10.5f);
    nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor (vg, busy ? palette::control : alpha (palette::mutedText, 0.85f));
    nvgText (vg, x + 16.0f, y + headerHeight - 10.0f, status.toRawUTF8(), nullptr);

    // Rows.
    const auto listTop = y + headerHeight;
    const auto listHeight = h - headerHeight - 30.0f;
    nvgSave (vg);
    nvgScissor (vg, x, listTop, w, listHeight);
    const auto count = rowCount();
    for (int row = 0; row < count; ++row)
    {
        const auto top = listTop + static_cast<float> (row) * rowHeight - scrollOffset;
        if (top + rowHeight < listTop || top > listTop + listHeight)
            continue;
        if (row == selected || row == hover)
        {
            nvgBeginPath (vg);
            nvgRoundedRect (vg, x + 10.0f, top + 1.0f, w - 20.0f, rowHeight - 2.0f, 4.0f);
            nvgFillColor (vg, row == selected ? alpha (palette::control, 0.35f) : nvgRGBAf (1, 1, 1, 0.05f));
            nvgFill (vg);
        }
        juce::String left, right;
        auto textX = x + 20.0f;
        if (view == View::tones)
        {
            const auto& tone = page.tones[static_cast<std::size_t> (row)];
            // Photo thumbnail (fetched on first sight, cached for the session).
            requestImage (tone);
            const juce::Rectangle<float> thumb (x + 16.0f, top + 3.0f, thumbWidth, rowHeight - 6.0f);
            nvgBeginPath (vg);
            nvgRoundedRect (vg, thumb.getX(), thumb.getY(), thumb.getWidth(), thumb.getHeight(), 3.0f);
            if (const auto found = images.find (tone.id); found != images.end() && found->second > 0)
            {
                int iw = 0, ih = 0;
                nvgImageSize (vg, found->second, &iw, &ih);
                // Cover: scale the photo so it fills the thumb, centred.
                const auto scale = juce::jmax (thumb.getWidth() / static_cast<float> (juce::jmax (1, iw)), thumb.getHeight() / static_cast<float> (juce::jmax (1, ih)));
                const auto pw = static_cast<float> (iw) * scale, ph = static_cast<float> (ih) * scale;
                nvgFillPaint (vg, nvgImagePattern (vg, thumb.getCentreX() - pw * 0.5f, thumb.getCentreY() - ph * 0.5f, pw, ph, 0.0f, found->second, 1.0f));
            }
            else
                nvgFillColor (vg, nvgRGBAf (1, 1, 1, 0.05f));
            nvgFill (vg);
            textX = thumb.getRight() + 10.0f;
            left = tone.title;
            right = (tone.make.isNotEmpty() ? tone.make + "  ·  " : juce::String()) + tone.gear
                  + (tone.modelsCount > 0 ? "  ·  " + juce::String (tone.modelsCount) + (tone.modelsCount == 1 ? " model" : " models") : juce::String())
                  + (tone.user.isNotEmpty() ? "  ·  " + tone.user : juce::String());
        }
        else
        {
            const auto& model = models[static_cast<std::size_t> (row)];
            left = model.name;
            right = model.size + (model.architecture > 0 ? "  ·  A" + juce::String (model.architecture) : juce::String());
        }
        nvgFontSize (vg, 12.5f);
        nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor (vg, palette::text);
        nvgText (vg, textX, top + rowHeight * 0.5f, left.toRawUTF8(), nullptr);
        nvgFontSize (vg, 10.0f);
        nvgTextAlign (vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgFillColor (vg, alpha (palette::mutedText, 0.9f));
        nvgText (vg, x + w - 20.0f, top + rowHeight * 0.5f, right.toRawUTF8(), nullptr);
    }
    nvgRestore (vg);

    nvgFontSize (vg, 9.5f);
    nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor (vg, alpha (palette::mutedText, 0.7f));
    nvgText (vg, x + 16.0f, y + h - 14.0f,
             view == View::models ? "Enter / double-click loads it into the module and keeps the panel open - compare, then Esc"
                                  : "type to search   Enter searches or opens the selected tone   Tab gear   Shift+Tab sort   Left / Right pages   Esc closes", nullptr);
}
} // namespace signalpatch::v2
