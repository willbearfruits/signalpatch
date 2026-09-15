#include "Tone3000.h"

#include <cstring>

#if JUCE_LINUX || JUCE_MAC
 #include <sys/stat.h>
#endif

namespace signalpatch::tone3000
{
juce::String base64Url (const void* data, size_t size)
{
    return juce::Base64::toBase64 (data, size).replaceCharacter ('+', '-').replaceCharacter ('/', '_').removeCharacters ("=");
}

juce::String Pkce::challengeFor (const juce::String& verifier)
{
    const juce::SHA256 hash (verifier.toRawUTF8(), verifier.getNumBytesAsUTF8());
    const auto raw = hash.getRawData();
    return base64Url (raw.getData(), raw.getSize());
}

Pkce Pkce::generate()
{
    auto& random = juce::Random::getSystemRandom();
    juce::uint8 bytes[32];
    for (auto& byte : bytes)
        byte = static_cast<juce::uint8> (random.nextInt (256));
    Pkce pkce;
    pkce.verifier = base64Url (bytes, sizeof (bytes));
    pkce.challenge = challengeFor (pkce.verifier);
    for (auto& byte : bytes)
        byte = static_cast<juce::uint8> (random.nextInt (256));
    pkce.state = base64Url (bytes, 16);
    return pkce;
}

juce::URL authorizeUrl (const juce::String& clientId, const juce::String& redirect, const Pkce& pkce)
{
    return juce::URL (juce::String (apiBase) + "/oauth/authorize")
        .withParameter ("client_id", clientId)
        .withParameter ("redirect_uri", redirect)
        .withParameter ("response_type", "code")
        .withParameter ("code_challenge", pkce.challenge)
        .withParameter ("code_challenge_method", "S256")
        .withParameter ("state", pkce.state)
        .withParameter ("format", "nam");
}

juce::String codeFromCallback (const juce::String& requestLine, const juce::String& expectedState)
{
    // "GET /cb?code=abc&state=xyz HTTP/1.1"
    const auto path = requestLine.fromFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf (" ", false, false);
    const auto query = path.fromFirstOccurrenceOf ("?", false, false);
    juce::String code, state;
    for (const auto& pair : juce::StringArray::fromTokens (query, "&", {}))
    {
        const auto key = pair.upToFirstOccurrenceOf ("=", false, false);
        const auto value = juce::URL::removeEscapeChars (pair.fromFirstOccurrenceOf ("=", false, false));
        if (key == "code") code = value;
        else if (key == "state") state = value;
    }
    return state == expectedState ? code : juce::String();
}

juce::var Tokens::toJson() const
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("access_token", access);
    object->setProperty ("refresh_token", refresh);
    object->setProperty ("expires_at_ms", expiresAtMs);
    return juce::var (object);
}

Tokens Tokens::fromJson (const juce::var& value)
{
    Tokens tokens;
    if (const auto* object = value.getDynamicObject())
    {
        tokens.access = object->getProperty ("access_token").toString();
        tokens.refresh = object->getProperty ("refresh_token").toString();
        tokens.expiresAtMs = static_cast<juce::int64> (object->getProperty ("expires_at_ms"));
    }
    return tokens;
}

Tokens Tokens::fromTokenResponse (const juce::var& response, juce::int64 nowMs)
{
    Tokens tokens;
    if (const auto* object = response.getDynamicObject())
    {
        tokens.access = object->getProperty ("access_token").toString();
        tokens.refresh = object->getProperty ("refresh_token").toString();
        const auto expiresIn = static_cast<juce::int64> (object->getProperty ("expires_in"));
        tokens.expiresAtMs = expiresIn > 0 ? nowMs + expiresIn * 1000 : 0;
    }
    return tokens;
}

namespace
{
    juce::String firstString (const juce::var& object, std::initializer_list<const char*> keys)
    {
        if (const auto* dynamic = object.getDynamicObject())
            for (const auto* key : keys)
                if (dynamic->hasProperty (key) && ! dynamic->getProperty (key).isVoid())
                    return dynamic->getProperty (key).toString();
        return {};
    }

    const juce::Array<juce::var>* listOf (const juce::var& value)
    {
        if (const auto* array = value.getArray())
            return array;
        if (const auto* object = value.getDynamicObject())
            if (const auto* array = object->getProperty ("data").getArray())
                return array;
        return nullptr;
    }
} // namespace

TonePage parseTonePage (const juce::var& value)
{
    TonePage page;
    if (const auto* object = value.getDynamicObject())
    {
        page.page = juce::jmax (1, static_cast<int> (object->getProperty ("page")));
        page.totalPages = juce::jmax (1, static_cast<int> (object->getProperty ("total_pages")));
        page.total = static_cast<juce::int64> (object->getProperty ("total"));
    }
    if (const auto* list = listOf (value))
        for (const auto& entry : *list)
        {
            const auto* object = entry.getDynamicObject();
            if (object == nullptr)
                continue;
            Tone tone;
            tone.id = static_cast<juce::int64> (object->getProperty ("id"));
            tone.title = firstString (entry, { "title", "name" });
            tone.gear = firstString (entry, { "gear" });
            tone.modelsCount = static_cast<int> (object->getProperty ("models_count"));
            if (const auto* makes = object->getProperty ("makes").getArray(); makes != nullptr && ! makes->isEmpty())
                tone.make = firstString (makes->getFirst(), { "name", "title", "make" });
            else
                tone.make = firstString (entry, { "make" });
            tone.user = firstString (object->getProperty ("user"), { "username", "name", "display_name" });
            if (const auto* images = object->getProperty ("images").getArray(); images != nullptr && ! images->isEmpty())
                tone.image = images->getFirst().toString();
            if (tone.id != 0)
                page.tones.push_back (std::move (tone));
        }
    return page;
}

std::vector<Model> parseModels (const juce::var& value)
{
    std::vector<Model> models;
    if (const auto* list = listOf (value))
        for (const auto& entry : *list)
        {
            const auto* object = entry.getDynamicObject();
            if (object == nullptr)
                continue;
            Model model;
            model.id = static_cast<juce::int64> (object->getProperty ("id"));
            model.name = firstString (entry, { "name", "title" });
            model.url = firstString (entry, { "model_url", "url" });
            model.size = firstString (entry, { "size" });
            model.architecture = static_cast<int> (object->getProperty ("architecture_version"));
            if (model.url.isNotEmpty())
                models.push_back (std::move (model));
        }
    return models;
}

juce::String modelFileName (const Tone& tone, const Model& model, const juce::String& extension)
{
    auto name = tone.title.trim();
    if (model.name.trim().isNotEmpty() && model.name.trim() != tone.title.trim())
        name += " - " + model.name.trim();
    if (model.size.isNotEmpty())
        name += " (" + model.size + ")";
    return juce::File::createLegalFileName (name).substring (0, 120) + "." + extension;
}

juce::File keysFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("SignalPatch").getChildFile ("tone3000.json");
}

juce::File tokensFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("SignalPatch").getChildFile ("tone3000-tokens.json");
}

juce::String loadPublishableKey()
{
    const auto parsed = juce::JSON::parse (keysFile());
    return firstString (parsed, { "publishableKey", "publishable_key", "client_id" });
}

Tokens loadTokens()
{
    return Tokens::fromJson (juce::JSON::parse (tokensFile()));
}

void saveTokens (const Tokens& tokens)
{
    tokensFile().getParentDirectory().createDirectory();
    tokensFile().replaceWithText (juce::JSON::toString (tokens.toJson()));
   #if JUCE_LINUX || JUCE_MAC
    chmod (tokensFile().getFullPathName().toRawUTF8(), 0600);
   #endif
}

void forgetTokens()
{
    tokensFile().deleteFile();
}

// ---------------------------------------------------------------- Client

juce::Result Client::get (const juce::String& path, const juce::StringPairArray& params, juce::var& out)
{
    auto url = juce::URL (juce::String (apiBase) + path);
    for (const auto& key : params.getAllKeys())
        url = url.withParameter (key, params[key]);
    int status = 0;
    auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                             .withExtraHeaders (authHeader())
                                             .withConnectionTimeoutMs (15000)
                                             .withStatusCode (&status));
    if (stream == nullptr)
        return juce::Result::fail ("No connection to tone3000.com");
    const auto body = stream->readEntireStreamAsString();
    if (status < 200 || status >= 300)
        return juce::Result::fail ("TONE3000 " + juce::String (status) + (status == 401 ? " - log in again" : "") + ": " + body.substring (0, 160));
    out = juce::JSON::parse (body);
    return juce::Result::ok();
}

juce::Result Client::postForm (const juce::String& path, const juce::StringPairArray& form, juce::var& out)
{
    juce::String body;
    for (const auto& key : form.getAllKeys())
        body += (body.isEmpty() ? "" : "&") + juce::URL::addEscapeChars (key, true) + "=" + juce::URL::addEscapeChars (form[key], true);
    int status = 0;
    auto stream = juce::URL (juce::String (apiBase) + path).withPOSTData (body)
                      .createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                              .withExtraHeaders ("Content-Type: application/x-www-form-urlencoded\r\nAccept: application/json\r\n")
                                              .withConnectionTimeoutMs (15000)
                                              .withStatusCode (&status));
    if (stream == nullptr)
        return juce::Result::fail ("No connection to tone3000.com");
    const auto text = stream->readEntireStreamAsString();
    if (status < 200 || status >= 300)
        return juce::Result::fail ("TONE3000 " + juce::String (status) + ": " + text.substring (0, 160));
    out = juce::JSON::parse (text);
    return juce::Result::ok();
}

juce::Result Client::exchangeCode (const juce::String& code, const juce::String& verifier, const juce::String& redirect)
{
    juce::StringPairArray form;
    form.set ("grant_type", "authorization_code");
    form.set ("code", code);
    form.set ("code_verifier", verifier);
    form.set ("redirect_uri", redirect);
    form.set ("client_id", clientId);
    juce::var response;
    const auto result = postForm ("/oauth/token", form, response);
    if (result.failed())
        return result;
    tokens = Tokens::fromTokenResponse (response, juce::Time::currentTimeMillis());
    return tokens.present() ? juce::Result::ok() : juce::Result::fail ("TONE3000 sent no access token");
}

juce::Result Client::refreshIfNeeded()
{
    if (! tokens.present())
        return juce::Result::fail ("Not logged in to TONE3000");
    if (! tokens.expired (juce::Time::currentTimeMillis()))
        return juce::Result::ok();
    if (tokens.refresh.isEmpty())
        return juce::Result::fail ("TONE3000 login expired - log in again");
    juce::StringPairArray form;
    form.set ("grant_type", "refresh_token");
    form.set ("refresh_token", tokens.refresh);
    form.set ("client_id", clientId);
    juce::var response;
    const auto result = postForm ("/oauth/token", form, response);
    if (result.failed())
        return result;
    tokens = Tokens::fromTokenResponse (response, juce::Time::currentTimeMillis());
    return tokens.present() ? juce::Result::ok() : juce::Result::fail ("TONE3000 refresh returned no token");
}

juce::Result Client::search (const juce::String& query, int page, const SearchOptions& options, TonePage& out)
{
    juce::StringPairArray params;
    if (query.trim().isNotEmpty())
        params.set ("query", query.trim());
    params.set ("format", options.format.isEmpty() ? "nam" : options.format);
    if (options.gear.isNotEmpty())
        params.set ("gear", options.gear);
    params.set ("page", juce::String (juce::jmax (1, page)));
    params.set ("page_size", juce::String (juce::jlimit (1, 100, options.pageSize)));
    if (options.sort.isNotEmpty())
        params.set ("sort", options.sort);
    juce::var response;
    const auto result = get ("/tones/search", params, response);
    if (result.failed())
        return result;
    out = parseTonePage (response);
    return juce::Result::ok();
}

juce::Result Client::models (juce::int64 toneId, std::vector<Model>& out)
{
    juce::StringPairArray params;
    params.set ("tone_id", juce::String (toneId));
    params.set ("page_size", "50");
    juce::var response;
    const auto result = get ("/models", params, response);
    if (result.failed())
        return result;
    out = parseModels (response);
    return juce::Result::ok();
}

juce::Result Client::download (const Model& model, const juce::File& destination)
{
    int status = 0;
    auto stream = juce::URL (model.url).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                              .withExtraHeaders (authHeader())
                                                              .withConnectionTimeoutMs (20000)
                                                              .withNumRedirectsToFollow (5)
                                                              .withStatusCode (&status));
    if (stream == nullptr)
        return juce::Result::fail ("No connection for the model download");
    juce::MemoryBlock data;
    stream->readIntoMemoryBlock (data);
    if (status < 200 || status >= 300)
        return juce::Result::fail ("Model download failed (" + juce::String (status) + ")");
    const bool json = data.getSize() >= 32 && data[0] == '{';
    const bool riff = data.getSize() >= 44 && std::memcmp (data.getData(), "RIFF", 4) == 0;
    if (! json && ! riff)
        return juce::Result::fail ("The download is neither a .nam nor a .wav");
    destination.getParentDirectory().createDirectory();
    return destination.replaceWithData (data.getData(), data.getSize()) ? juce::Result::ok() : juce::Result::fail ("Could not write " + destination.getFullPathName());
}

juce::Result Client::fetchBytes (const juce::String& url, juce::MemoryBlock& out)
{
    int status = 0;
    auto stream = juce::URL (url).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                         .withConnectionTimeoutMs (10000)
                                                         .withNumRedirectsToFollow (5)
                                                         .withStatusCode (&status));
    if (stream == nullptr)
        return juce::Result::fail ("no connection");
    stream->readIntoMemoryBlock (out);
    return status >= 200 && status < 300 ? juce::Result::ok() : juce::Result::fail ("http " + juce::String (status));
}

// ---------------------------------------------------------------- CallbackServer

bool CallbackServer::start (juce::String expectedState, std::function<void (juce::String)> onCode)
{
    stop();
    state = std::move (expectedState);
    callback = std::move (onCode);
    if (! listener.createListener (callbackPort, "127.0.0.1"))
        return false;
    startThread();
    return true;
}

void CallbackServer::stop()
{
    signalThreadShouldExit();
    listener.close();
    stopThread (2000);
}

void CallbackServer::run()
{
    while (! threadShouldExit())
    {
        if (listener.waitUntilReady (true, 200) != 1)
            continue;
        std::unique_ptr<juce::StreamingSocket> connection (listener.waitForNextConnection());
        if (connection == nullptr)
            continue;
        char buffer[4096] {};
        juce::String request;
        for (int attempt = 0; attempt < 10 && ! request.contains ("\r\n"); ++attempt)
        {
            if (connection->waitUntilReady (true, 500) != 1)
                continue;
            const auto read = connection->read (buffer, sizeof (buffer) - 1, false);
            if (read <= 0)
                break;
            request += juce::String::fromUTF8 (buffer, read);
        }
        const auto requestLine = request.upToFirstOccurrenceOf ("\r\n", false, false);
        const auto code = codeFromCallback (requestLine, state);
        const juce::String page = code.isNotEmpty()
            ? "<html><head><title>SignalPatch - TONE3000</title></head><body style='font-family:sans-serif;background:#14181c;color:#eee;padding:40px'><h2>SignalPatch is connected to TONE3000.</h2><p>You can close this tab and go back to the rig.</p></body></html>"
            : "<html><head><title>SignalPatch - TONE3000</title></head><body style='font-family:sans-serif;background:#14181c;color:#eee;padding:40px'><h2>That did not work.</h2><p>Go back to SignalPatch and try the login again.</p></body></html>";
        const auto response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " + juce::String (page.getNumBytesAsUTF8())
                            + "\r\nConnection: close\r\n\r\n" + page;
        connection->write (response.toRawUTF8(), static_cast<int> (response.getNumBytesAsUTF8()));
        connection->close();
        if (code.isNotEmpty())
        {
            if (callback)
                callback (code);
            break;
        }
    }
    listener.close();
}
} // namespace signalpatch::tone3000
