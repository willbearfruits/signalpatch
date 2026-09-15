#pragma once

#include <JuceHeader.h>

#include <vector>

// TONE3000 (tone3000.com): browse and download NAM captures from inside the
// app. Public-client OAuth with PKCE against https://www.tone3000.com/api/v1,
// the browser does the login and comes back to a localhost listener. The
// parsing / PKCE / URL pieces are pure and tested; Client and CallbackServer
// do the blocking network work and must run off the message thread.
namespace signalpatch::tone3000
{
inline const char* apiBase = "https://www.tone3000.com/api/v1";
constexpr int callbackPort = 41443;
inline juce::String redirectUri() { return "http://localhost:" + juce::String (callbackPort) + "/cb"; }

juce::String base64Url (const void* data, size_t size);

struct Pkce
{
    juce::String verifier, challenge, state;
    static Pkce generate();
    static juce::String challengeFor (const juce::String& verifier);
};

juce::URL authorizeUrl (const juce::String& clientId, const juce::String& redirect, const Pkce& pkce);

/** The code from the browser's "GET /cb?code=...&state=... HTTP/1.1" line; empty when the state does not match. */
juce::String codeFromCallback (const juce::String& requestLine, const juce::String& expectedState);

struct Tokens
{
    juce::String access, refresh;
    juce::int64 expiresAtMs = 0;
    [[nodiscard]] bool present() const noexcept { return access.isNotEmpty(); }
    [[nodiscard]] bool expired (juce::int64 nowMs) const noexcept { return expiresAtMs > 0 && nowMs > expiresAtMs - 30000; }
    [[nodiscard]] juce::var toJson() const;
    static Tokens fromJson (const juce::var& value);
    static Tokens fromTokenResponse (const juce::var& response, juce::int64 nowMs);
};

struct Tone
{
    juce::int64 id = 0;
    juce::String title, make, gear, user;
    int modelsCount = 0;
};

struct TonePage
{
    std::vector<Tone> tones;
    int page = 1, totalPages = 1;
    juce::int64 total = 0;
};

struct Model
{
    juce::int64 id = 0;
    juce::String name, url, size;
    int architecture = 0;
};

TonePage parseTonePage (const juce::var& value);
std::vector<Model> parseModels (const juce::var& value);
/** "Title - Model name (size).nam", safe for a file system. */
juce::String modelFileName (const Tone& tone, const Model& model);

juce::File keysFile();
juce::File tokensFile();
juce::String loadPublishableKey();
Tokens loadTokens();
void saveTokens (const Tokens& tokens);
void forgetTokens();

/** Blocking HTTP; call from a worker thread. */
class Client
{
public:
    explicit Client (juce::String publishableKey) : clientId (std::move (publishableKey)) {}

    Tokens tokens;
    juce::Result exchangeCode (const juce::String& code, const juce::String& verifier, const juce::String& redirect);
    juce::Result refreshIfNeeded();
    juce::Result search (const juce::String& query, int page, TonePage& out);
    juce::Result models (juce::int64 toneId, std::vector<Model>& out);
    juce::Result download (const Model& model, const juce::File& destination);

private:
    juce::Result get (const juce::String& path, const juce::StringPairArray& params, juce::var& out);
    juce::Result postForm (const juce::String& path, const juce::StringPairArray& form, juce::var& out);
    [[nodiscard]] juce::String authHeader() const { return "Authorization: Bearer " + tokens.access + "\r\nAccept: application/json\r\n"; }

    juce::String clientId;
};

/** Listens on 127.0.0.1:callbackPort for the one browser redirect, hands the code to onCode (on this thread). */
class CallbackServer final : private juce::Thread
{
public:
    CallbackServer() : juce::Thread ("tone3000 callback") {}
    ~CallbackServer() override { stop(); }
    bool start (juce::String expectedState, std::function<void (juce::String code)> onCode);
    void stop();
    [[nodiscard]] bool isListening() const noexcept { return isThreadRunning(); }

private:
    void run() override;
    juce::StreamingSocket listener;
    juce::String state;
    std::function<void (juce::String)> callback;
};
} // namespace signalpatch::tone3000
