#include "GmailClient.h"

#include <cstring>
#include <random>
#include <string>
#include <vector>
#include <algorithm>
#include <optional>
#if defined(LOOPBRIDGE_GMAIL_TESTS)
 #include <iostream>
#endif

#if JUCE_WINDOWS
 #include <winsock2.h>
 #include <ws2tcpip.h>
 #pragma comment(lib, "Ws2_32.lib")
#endif

namespace
{
    constexpr const char* authorizationEndpoint =
        "https://accounts.google.com/o/oauth2/v2/auth";

    constexpr const char* tokenEndpoint =
        "https://oauth2.googleapis.com/token";

    constexpr const char* gmailScope =
        "https://www.googleapis.com/auth/gmail.readonly";

    constexpr const char* gmailApiBase =
        "https://gmail.googleapis.com/gmail/v1/users/me";

    juce::File findProjectFile(
        const juce::String& filename)
    {
        auto current =
            juce::File::getSpecialLocation(
                juce::File::currentApplicationFile);

        if (current.existsAsFile())
        {
            current =
                current.getParentDirectory();
        }

        for (int i = 0; i < 10; ++i)
        {
            const auto candidate =
                current.getChildFile(
                    filename);

            if (candidate.existsAsFile())
            {
                return candidate;
            }

            const auto parent =
                current.getParentDirectory();

            if (parent == current)
            {
                break;
            }

            current = parent;
        }

        auto cwd =
            juce::File::
                getCurrentWorkingDirectory();

        for (int i = 0; i < 10; ++i)
        {
            const auto candidate =
                cwd.getChildFile(
                    filename);

            if (candidate.existsAsFile())
            {
                return candidate;
            }

            const auto parent =
                cwd.getParentDirectory();

            if (parent == cwd)
            {
                break;
            }

            cwd = parent;
        }

        return {};
    }

    juce::String urlEncode(
        const juce::String& value)
    {
        return juce::URL::addEscapeChars(
            value,
            true);
    }

    juce::String sanitiseFilename(
        const juce::String& filename)
    {
        juce::String result =
            filename.trim();

        if (result.isEmpty())
        {
            result =
                "loopbridge-audio";
        }

        const juce::String invalidCharacters =
            "\\/:*?\"<>|";

        for (auto character
             : invalidCharacters)
        {
            result =
                result.replaceCharacter(
                    character,
                    '_');
        }

        return result;
    }

    bool decodeBase64Url(
        juce::String encoded,
        juce::MemoryBlock& output)
    {
        encoded = encoded.trim();

        if (encoded.isEmpty())
            return false;

        output.reset();

        std::vector<unsigned char> decoded;
        decoded.reserve(
            static_cast<size_t>(
                encoded.length() * 3 / 4));

        unsigned int buffer = 0;
        int bitsInBuffer = 0;

        for (auto character : encoded)
        {
            int value = -1;

            if (character >= 'A'
                && character <= 'Z')
            {
                value = character - 'A';
            }
            else if (character >= 'a'
                    && character <= 'z')
            {
                value =
                    character - 'a' + 26;
            }
            else if (character >= '0'
                    && character <= '9')
            {
                value =
                    character - '0' + 52;
            }
            else if (character == '-'
                    || character == '+')
            {
                value = 62;
            }
            else if (character == '_'
                    || character == '/')
            {
                value = 63;
            }
            else if (character == '=')
            {
                break;
            }
            else
            {
                return false;
            }

            buffer =
                (buffer << 6)
                | static_cast<unsigned int>(
                    value);

            bitsInBuffer += 6;

            if (bitsInBuffer >= 8)
            {
                bitsInBuffer -= 8;

                decoded.push_back(
                    static_cast<unsigned char>(
                        (buffer >> bitsInBuffer)
                        & 0xff));
            }
        }

        if (decoded.empty())
            return false;

        output.append(
            decoded.data(),
            decoded.size());

        return true;
    }

#if JUCE_WINDOWS

    class SocketGuard
    {
    public:
        SocketGuard()
        {
            WSADATA data {};

            ok =
                WSAStartup(
                    MAKEWORD(2, 2),
                    &data)
                == 0;
        }

        ~SocketGuard()
        {
            if (ok)
            {
                WSACleanup();
            }
        }

        bool isOk() const
        {
            return ok;
        }

    private:
        bool ok = false;
    };

    bool sendAll(
        SOCKET socketHandle,
        const std::string& text)
    {
        size_t sent = 0;

        while (sent < text.size())
        {
            const int result =
                send(
                    socketHandle,
                    text.data() + sent,
                    static_cast<int>(
                        text.size() - sent),
                    0);

            if (result <= 0)
            {
                return false;
            }

            sent +=
                static_cast<size_t>(
                    result);
        }

        return true;
    }

    juce::String readHttpRequest(
        SOCKET socketHandle, juce::Thread& thread)
    {
        std::string request;

        char buffer[2048];
        const double deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;

        while (
            request.find("\r\n\r\n")
            == std::string::npos && !thread.threadShouldExit()
            && juce::Time::getMillisecondCounterHiRes() < deadline)
        {
            const int received =
                recv(
                    socketHandle,
                    buffer,
                    static_cast<int>(
                        sizeof(buffer)),
                    0);

            if (received <= 0)
            {
                break;
            }

            request.append(
                buffer,
                static_cast<size_t>(
                    received));

            if (request.size() > 65536)
            {
                break;
            }
        }

        return juce::String::fromUTF8(
            request.data(),
            static_cast<int>(
                request.size()));
    }

    juce::String getQueryParameter(
        const juce::String& request,
        const juce::String& parameter)
    {
        const int firstSpace =
            request.indexOfChar(' ');

        if (firstSpace < 0)
        {
            return {};
        }

        const int secondSpace =
            request.indexOf(
                firstSpace + 1,
                " ");

        if (secondSpace < 0)
        {
            return {};
        }

        const auto target =
            request.substring(
                firstSpace + 1,
                secondSpace);

        const int question =
            target.indexOfChar('?');

        if (question < 0)
        {
            return {};
        }

        const auto query =
            target.substring(
                question + 1);

        juce::StringArray pairs;

        pairs.addTokens(
            query,
            "&",
            "");

        for (const auto& pair : pairs)
        {
            const int equals =
                pair.indexOfChar('=');

            if (equals < 0)
            {
                continue;
            }

            const auto name =
                pair.substring(
                    0,
                    equals);

            if (name != parameter)
            {
                continue;
            }

            return juce::URL::
                removeEscapeChars(
                    pair.substring(
                        equals + 1));
        }

        return {};
    }

#endif
}

bool GmailClient::DiscoveryIndex::load(const juce::File& file)
{
    const auto json = juce::JSON::parse(file.loadFileAsString());
    if (!json.isObject() || static_cast<int>(json["version"]) != 1
        || !json["account"].isString() || json["account"].toString().trim().isEmpty()
        || !json["messages"].isArray())
        return false;

    DiscoveryIndex restored;
    restored.account = json["account"].toString().trim().toLowerCase();
    for (const auto& row : *json["messages"].getArray())
    {
        if (!row.isObject() || !row["id"].isString() || row["id"].toString().isEmpty()
            || !row["sender"].isString() || !row["subject"].isString()
            || !row["attachments"].isArray() || restored.knows(row["id"].toString()))
            return false;
        Message message { row["id"].toString(), row["sender"].toString(),
                          row["subject"].toString(), {} };
        for (const auto& entry : *row["attachments"].getArray())
        {
            if (!entry.isObject() || !entry["id"].isString() || entry["id"].toString().isEmpty()
                || !entry["filename"].isString() || entry["filename"].toString().isEmpty())
                return false;
            message.attachments.push_back({ message.id, entry["id"].toString(),
                entry["filename"].toString(), message.sender, message.subject });
        }
        restored.messages.push_back(std::move(message));
    }
    *this = std::move(restored);
    return true;
}

bool GmailClient::DiscoveryIndex::save(const juce::File& file) const
{
    if (account.isEmpty() || file.getParentDirectory().createDirectory().failed())
        return false;
    auto* root = new juce::DynamicObject();
    juce::var json(root);
    root->setProperty("version", 1);
    root->setProperty("account", account);
    juce::Array<juce::var> rows;
    for (const auto& message : messages)
    {
        auto* row = new juce::DynamicObject();
        juce::var value(row);
        row->setProperty("id", message.id);
        row->setProperty("sender", message.sender);
        row->setProperty("subject", message.subject);
        juce::Array<juce::var> entries;
        for (const auto& attachment : message.attachments)
        {
            auto* entry = new juce::DynamicObject();
            juce::var item(entry);
            entry->setProperty("id", attachment.attachmentId);
            entry->setProperty("filename", attachment.filename);
            entries.add(item);
        }
        row->setProperty("attachments", juce::var(entries));
        rows.add(value);
    }
    root->setProperty("messages", juce::var(rows));
    // A sibling temporary file keeps replacement on the same filesystem.
    juce::TemporaryFile staging(file);
    return staging.getFile().replaceWithText(juce::JSON::toString(json, true))
        && staging.overwriteTargetFileWithTemporary();
}

bool GmailClient::DiscoveryIndex::knows(const juce::String& id) const
{
    return std::any_of(messages.begin(), messages.end(),
        [&id](const Message& message) { return message.id == id; });
}

std::vector<GmailClient::AudioAttachment> GmailClient::DiscoveryIndex::attachments() const
{
    std::vector<AudioAttachment> result;
    for (const auto& message : messages)
        result.insert(result.end(), message.attachments.begin(), message.attachments.end());
    return result;
}

class GmailClient::Worker final : public juce::Thread
{
public:
    struct Job
    {
        juce::String key;
        GmailClient::Priority priority;
        uint64_t generation;
        std::function<void(Worker&)> run;
        std::function<void()> cancelled;
        bool explicitDownload = false;
    };

    explicit Worker(std::shared_ptr<Lifetime> state, const juce::File& file = {})
        : juce::Thread("LoopBridge Gmail requests"), indexFile(file), lifetime(std::move(state))
    {
        startThread();
    }

    ~Worker() override
    {
        signalThreadShouldExit();
        notify();
        {
            const juce::ScopedLock lock(streamLock);
            if (activeStream != nullptr)
                activeStream->cancel();
        }
        // Never forcibly terminate a thread that owns files or HTTP resources.
        stopThread(-1);
    }

    void enqueue(Job job)
    {
        const juce::ScopedLock lock(queueLock);
        job.explicitDownload = job.priority == GmailClient::Priority::download && job.key.isNotEmpty();
        jobs.push_back(std::move(job));
        notify();
    }

    void promote(const juce::String& key, GmailClient::Priority priority)
    {
        const juce::ScopedLock lock(queueLock);
        for (auto& job : jobs)
            if (job.key == key)
            {
                if (priority == GmailClient::Priority::download)
                    job.explicitDownload = true;
                if (priority < job.priority)
                    job.priority = priority;
            }
        notify();
    }

    std::vector<juce::String> discardNavigation(const juce::String& keepKey)
    {
        std::vector<Job> removed;
        std::vector<juce::String> keys;
        {
            const juce::ScopedLock lock(queueLock);
            for (auto it = jobs.begin(); it != jobs.end();)
            {
                if (it->key != keepKey && it->explicitDownload
                    && it->priority == GmailClient::Priority::preview)
                    it->priority = GmailClient::Priority::download;
                if (it->key.isNotEmpty() && it->key != keepKey && !it->explicitDownload
                    && (it->priority == GmailClient::Priority::prefetch
                        || it->priority == GmailClient::Priority::preview))
                {
                    keys.push_back(it->key);
                    removed.push_back(std::move(*it));
                    it = jobs.erase(it);
                }
                else
                    ++it;
            }
        }
        for (auto& job : removed)
            if (job.cancelled)
                job.cancelled();
        return keys;
    }

    bool cancelled() const
    {
        return threadShouldExit() || !lifetime->alive.load()
            || runningGeneration != lifetime->generation.load();
    }

    bool delay(int milliseconds)
    {
        const double until = juce::Time::getMillisecondCounterHiRes() + milliseconds;
        while (!cancelled() && juce::Time::getMillisecondCounterHiRes() < until)
            wait(100);
        return !cancelled();
    }

    struct Response
    {
        int status = 0;
        juce::StringPairArray headers;
        juce::String body;
    };
#if defined(LOOPBRIDGE_GMAIL_TESTS)
    std::function<Response(const juce::URL&)> testRequest;
#endif

    Response request(const juce::URL& url, const juce::String& headers,
                     const juce::String& command)
    {
        Response response;
        if (cancelled())
            return response;
#if defined(LOOPBRIDGE_GMAIL_TESTS)
        if (testRequest)
            return testRequest(url);
#endif
        juce::WebInputStream stream(url, false);
        stream.withExtraHeaders(headers).withCustomRequestCommand(command)
            .withConnectionTimeout(15000);
        {
            const juce::ScopedLock lock(streamLock);
            if (cancelled())
                return response;
            activeStream = &stream;
        }
        if (stream.connect(nullptr))
        {
            response.status = stream.getStatusCode();
            response.headers = stream.getResponseHeaders();
            juce::MemoryOutputStream body;
            char buffer[16384];
            while (!cancelled() && !stream.isExhausted())
            {
                const int count = stream.read(buffer, sizeof(buffer));
                if (count <= 0)
                    break;
                body.write(buffer, static_cast<size_t>(count));
            }
            response.body = body.toUTF8();
            if (stream.isError())
                response.status = 0;
        }
        {
            const juce::ScopedLock lock(streamLock);
            activeStream = nullptr;
        }
        return response;
    }

    void run() override
    {
        while (!threadShouldExit())
        {
            Job job;
            bool found = false;
            {
                const juce::ScopedLock lock(queueLock);
                jobs.erase(std::remove_if(jobs.begin(), jobs.end(), [this](const Job& queued)
                    { return queued.generation != lifetime->generation.load(); }), jobs.end());
                if (!jobs.empty())
                {
                    auto next = std::min_element(jobs.begin(), jobs.end(),
                        [](const Job& a, const Job& b) { return a.priority < b.priority; });
                    job = std::move(*next);
                    jobs.erase(next);
                    runningGeneration = job.generation;
                    found = true;
                }
            }
            if (found && !cancelled())
            {
                lastError = {};
                job.run(*this);
            }
            else
                wait(100);
        }
    }

    // A library scan is many HTTP requests. Let explicit attachment work run
    // between messages rather than holding the worker for the entire scan.
    void runUrgentAttachments()
    {
        while (!cancelled())
        {
            Job job;
            {
                const juce::ScopedLock lock(queueLock);
                auto next = std::min_element(jobs.begin(), jobs.end(),
                    [](const Job& a, const Job& b) { return a.priority < b.priority; });
                if (next == jobs.end() || next->key.isEmpty()
                    || next->priority == GmailClient::Priority::prefetch
                    || next->generation != runningGeneration)
                    return;
                job = std::move(*next);
                jobs.erase(next);
            }
            const auto savedError = lastError;
            lastError = {};
            job.run(*this);
            lastError = savedError;
        }
    }

    // Accessed only by this worker; shared by listing and attachment requests.
    double cooldownUntil = 0.0;
    RequestError lastError, quotaError;
    DiscoveryIndex index;
    const juce::File indexFile;
    std::optional<uint64_t> verifiedGeneration;
    void loadIndex()
    {
        if (!indexLoaded)
        {
            index.load(indexFile);
            indexLoaded = true;
        }
    }

private:
    friend class GmailClient;
    std::shared_ptr<Lifetime> lifetime;
    uint64_t runningGeneration = 0;
    juce::CriticalSection queueLock, streamLock;
    std::vector<Job> jobs;
    juce::WebInputStream* activeStream = nullptr;
    bool indexLoaded = false;
};

class GmailAuthThread final
    : public juce::Thread
{
public:
    GmailAuthThread(
        GmailClient& ownerIn,
        int portIn)
        : juce::Thread(
              "LoopBridge Gmail OAuth"),
          owner(ownerIn),
          port(portIn),
          expectedState(ownerIn.stateToken),
          lifetime(ownerIn.lifetime),
          generation(ownerIn.getSessionGeneration())
    {
    }

    void run() override
    {
#if JUCE_WINDOWS
        SocketGuard sockets;

        if (!sockets.isOk())
        {
            finishWithError(
                "Could not initialise "
                "Windows networking.");

            return;
        }

        SOCKET serverSocket =
            socket(
                AF_INET,
                SOCK_STREAM,
                IPPROTO_TCP);

        if (serverSocket
            == INVALID_SOCKET)
        {
            finishWithError(
                "Could not create OAuth "
                "callback socket.");

            return;
        }

        sockaddr_in address {};

        address.sin_family =
            AF_INET;

        address.sin_addr.s_addr =
            htonl(INADDR_LOOPBACK);

        address.sin_port =
            htons(
                static_cast<u_short>(
                    port));

        if (bind(
                serverSocket,
                reinterpret_cast<
                    sockaddr*>(
                        &address),
                sizeof(address))
            == SOCKET_ERROR)
        {
            closesocket(
                serverSocket);

            finishWithError(
                "Could not bind OAuth "
                "callback port.");

            return;
        }

        if (listen(
                serverSocket,
                1)
            == SOCKET_ERROR)
        {
            closesocket(
                serverSocket);

            finishWithError(
                "Could not listen for "
                "OAuth callback.");

            return;
        }

        int ready = 0;
        const double deadline = juce::Time::getMillisecondCounterHiRes() + 180000.0;
        while (!threadShouldExit() && juce::Time::getMillisecondCounterHiRes() < deadline)
        {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(serverSocket, &readSet);
            timeval timeout {};
            timeout.tv_usec = 100000;
            ready = select(0, &readSet, nullptr, nullptr, &timeout);
            if (ready != 0)
                break;
        }

        if (ready <= 0)
        {
            closesocket(
                serverSocket);

            finishWithError(
                "Google sign-in timed out.");

            return;
        }

        SOCKET clientSocket =
            accept(
                serverSocket,
                nullptr,
                nullptr);

        closesocket(
            serverSocket);

        if (clientSocket
            == INVALID_SOCKET)
        {
            finishWithError(
                "Could not accept OAuth "
                "callback.");

            return;
        }

        const DWORD socketTimeout = 1000;
        setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char*>(&socketTimeout), sizeof(socketTimeout));
        setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO,
            reinterpret_cast<const char*>(&socketTimeout), sizeof(socketTimeout));

        const auto request =
            readHttpRequest(
                clientSocket, *this);

        const auto code =
            getQueryParameter(
                request,
                "code");

        const auto returnedState =
            getQueryParameter(
                request,
                "state");

        const auto error =
            getQueryParameter(
                request,
                "error");

        const bool valid =
            error.isEmpty()
            && code.isNotEmpty()
            && returnedState
                == expectedState;

        std::string body;

        if (valid)
        {
            body =
                "<html>"
                "<body style=\""
                "font-family:sans-serif;"
                "background:#111;"
                "color:#eee;"
                "padding:40px;\">"
                "<h2>LoopBridge connected.</h2>"
                "<p>You can close this window "
                "and return to LoopBridge.</p>"
                "</body>"
                "</html>";
        }
        else
        {
            body =
                "<html>"
                "<body style=\""
                "font-family:sans-serif;"
                "background:#111;"
                "color:#eee;"
                "padding:40px;\">"
                "<h2>LoopBridge connection "
                "failed.</h2>"
                "<p>You can close this window "
                "and return to LoopBridge.</p>"
                "</body>"
                "</html>";
        }

        const std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; "
            "charset=utf-8\r\n"
            "Connection: close\r\n"
            "Content-Length: "
            + std::to_string(
                body.size())
            + "\r\n\r\n"
            + body;

        sendAll(
            clientSocket,
            response);

        closesocket(
            clientSocket);

        if (error.isNotEmpty())
        {
            finishWithError(
                "Google authorization was "
                "cancelled: "
                + error);

            return;
        }

        if (returnedState
            != expectedState)
        {
            finishWithError(
                "OAuth state validation "
                "failed.");

            return;
        }

        if (code.isEmpty())
        {
            finishWithError(
                "Google did not return an "
                "authorization code.");

            return;
        }

        juce::MessageManager::callAsync(
            [client = &owner, state = lifetime, session = generation, code]
            {
                if (state->alive.load() && state->generation.load() == session)
                    client->handleAuthorizationCode(code);
            });
#else
        finishWithError(
            "This OAuth callback is "
            "currently implemented "
            "for Windows.");
#endif
    }

private:
    void finishWithError(
        const juce::String& message)
    {
        juce::MessageManager::callAsync(
            [client = &owner, state = lifetime, session = generation, message]
            {
                if (state->alive.load() && state->generation.load() == session)
                    client->setState(GmailClient::State::error, message);
            });
    }

    GmailClient& owner;

    int port = 0;
    const juce::String expectedState;
    std::shared_ptr<GmailClient::Lifetime> lifetime;
    const uint64_t generation;
};

GmailClient::GmailClient(const juce::File& indexFile)
    : worker(std::make_unique<Worker>(lifetime, indexFile == juce::File{}
        ? juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("LoopBridge").getChildFile("gmail-library.json")
        : indexFile)) {}

GmailClient::~GmailClient()
{
    shutdown();
}

void GmailClient::shutdown()
{
    lifetime->alive.store(false);
    ++lifetime->generation;
    pendingAttachments.clear();
    if (authThread != nullptr)
    {
        authThread
            ->signalThreadShouldExit();

        authThread
            ->stopThread(-1);
        authThread.reset();
    }
    worker.reset();
}

void GmailClient::connect(
    StateCallback callback)
{
    stateCallback =
        std::move(callback);

    if (state
        == State::authorizing)
    {
        return;
    }
    ++lifetime->generation;
    pendingAttachments.clear();
    worker->notify();
    librarySyncActive = false;
    accessToken.clear();
    unverifiedAccessToken.clear();
    refreshToken.clear();

    if (!loadCredentials(
            credentials))
    {
        setState(
            State::error,
            "credentials.json not found "
            "or invalid.");

        return;
    }

    beginAuthorization();
}

void GmailClient::disconnect()
{
    ++lifetime->generation;
    librarySyncActive = false;
    pendingAttachments.clear();
    worker->notify();
    if (authThread != nullptr)
    {
        authThread
            ->signalThreadShouldExit();

        authThread
            ->stopThread(-1);

        authThread.reset();
    }

    accessToken.clear();
    unverifiedAccessToken.clear();
    refreshToken.clear();

    setState(
        State::disconnected,
        "Not connected");
}

GmailClient::State
GmailClient::getState() const
{
    return state;
}

juce::String
GmailClient::getStatusText() const
{
    return statusText;
}

bool GmailClient::loadCredentials(
    OAuthCredentials& result)
{
    const auto file =
        findProjectFile(
            "credentials.json");

    if (!file.existsAsFile())
    {
        return false;
    }

    const auto json =
        juce::JSON::parse(
            file.loadFileAsString());

    if (!json.isObject())
    {
        return false;
    }

    auto* root =
        json.getDynamicObject();

    if (root == nullptr)
    {
        return false;
    }

    const auto installed =
        root->getProperty(
            "installed");

    if (!installed.isObject())
    {
        return false;
    }

    auto* installedObject =
        installed
            .getDynamicObject();

    if (installedObject == nullptr)
    {
        return false;
    }

    result.clientId =
        installedObject
            ->getProperty(
                "client_id")
            .toString();

    result.clientSecret =
        installedObject
            ->getProperty(
                "client_secret")
            .toString();

    return result.clientId
        .isNotEmpty();
}

juce::String
GmailClient::createCodeVerifier() const
{
    juce::Random random;

    juce::MemoryBlock bytes;

    bytes.setSize(48);

    random.fillBitsRandomly(
        bytes.getData(),
        bytes.getSize());

    return base64UrlEncode(
        bytes.getData(),
        bytes.getSize());
}

juce::String
GmailClient::createCodeChallenge(
    const juce::String& verifier) const
{
    const auto utf8 =
        verifier.toRawUTF8();

    juce::SHA256 hash(
        utf8,
        std::strlen(utf8));

    const auto raw =
        hash.getRawData();

    return base64UrlEncode(
        raw.getData(),
        raw.getSize());
}

juce::String
GmailClient::createStateToken() const
{
    juce::Random random;

    juce::MemoryBlock bytes;

    bytes.setSize(24);

    random.fillBitsRandomly(
        bytes.getData(),
        bytes.getSize());

    return base64UrlEncode(
        bytes.getData(),
        bytes.getSize());
}

void GmailClient::beginAuthorization()
{
    constexpr int port =
        53682;

    callbackPort =
        port;

    redirectUri =
        "http://127.0.0.1:"
        + juce::String(
            port);

    codeVerifier =
        createCodeVerifier();

    const auto challenge =
        createCodeChallenge(
            codeVerifier);

    stateToken =
        createStateToken();

    if (authThread != nullptr)
    {
        authThread
            ->stopThread(-1);

        authThread.reset();
    }

    authThread =
        std::make_unique<
            GmailAuthThread>(
                *this,
                callbackPort);

    authThread
        ->startThread();

    juce::String authorizationUrl =
        authorizationEndpoint;

    authorizationUrl
        << "?client_id="
        << urlEncode(
               credentials.clientId)
        << "&redirect_uri="
        << urlEncode(
               redirectUri)
        << "&response_type=code"
        << "&scope="
        << urlEncode(
               gmailScope)
        << "&code_challenge="
        << urlEncode(
               challenge)
        << "&code_challenge_method=S256"
        << "&state="
        << urlEncode(
               stateToken)
        << "&access_type=offline"
        << "&prompt=consent";

    setState(
        State::authorizing,
        "Waiting for Google sign-in...");

    juce::URL(
        authorizationUrl)
        .launchInDefaultBrowser();
}

void GmailClient::
    handleAuthorizationCode(
        const juce::String& code)
{
    setState(
        State::authorizing,
        "Finishing Google sign-in...");

    const auto session = getSessionGeneration();
    worker->enqueue({ {}, Priority::preview, session,
        [this, guard = lifetime, session, code, auth = credentials,
         verifier = codeVerifier, redirect = redirectUri](Worker& background)
        {
            TokenData tokens;
            const bool success = exchangeCodeForTokens(code, tokens, auth, verifier, redirect, background);
            juce::MessageManager::callAsync([this, guard, session, tokens, success]
            {
                if (!guard->alive.load() || guard->generation.load() != session)
                    return;
                if (!success)
                {
                    setState(State::error, "Could not exchange Google authorization code for tokens.");
                    return;
                }
                unverifiedAccessToken = tokens.accessToken;
                refreshToken = tokens.refreshToken;
                setState(State::connected, "Connected to Gmail");
            });
        }, {} });
}

bool GmailClient::
    exchangeCodeForTokens(
        const juce::String& code,
        TokenData& tokens, const OAuthCredentials& credentials,
        const juce::String& codeVerifier, const juce::String& redirectUri,
        Worker& worker)
{
    juce::String body;

    body
        << "client_id="
        << urlEncode(
               credentials.clientId)
        << "&code="
        << urlEncode(
               code)
        << "&code_verifier="
        << urlEncode(
               codeVerifier)
        << "&grant_type="
           "authorization_code"
        << "&redirect_uri="
        << urlEncode(
               redirectUri);

    if (credentials
            .clientSecret
            .isNotEmpty())
    {
        body
            << "&client_secret="
            << urlEncode(
                   credentials
                       .clientSecret);
    }

    juce::URL url(
        tokenEndpoint);

    url =
        url.withPOSTData(
            body);

    const auto response = worker.request(url,
        "Content-Type: application/x-www-form-urlencoded\r\n", "POST");
    if (response.status < 200 || response.status >= 300 || worker.cancelled())
        return false;

    const auto json =
        juce::JSON::parse(
            response.body);

    if (!json.isObject())
    {
        return false;
    }

    auto* object =
        json.getDynamicObject();

    if (object == nullptr)
    {
        return false;
    }

    tokens.accessToken =
        object
            ->getProperty(
                "access_token")
            .toString();

    tokens.refreshToken =
        object
            ->getProperty(
                "refresh_token")
            .toString();

    tokens.expiresIn =
        static_cast<int>(
            object
                ->getProperty(
                    "expires_in"));

    return tokens
        .accessToken
        .isNotEmpty();
}

bool GmailClient::performAuthorizedGet(
    const juce::String& requestUrl,
    juce::var& jsonResult,
    juce::String& errorMessage, const juce::String& accessToken, Worker& worker)
{
    if (accessToken.isEmpty())
    {
        errorMessage =
            "No Gmail access token is available.";

        return false;
    }

    if (juce::Time::getMillisecondCounterHiRes() < worker.cooldownUntil)
    {
        worker.lastError = worker.quotaError;
        errorMessage = "Gmail quota cooldown is active. " + worker.quotaError.message;
        return false;
    }

    for (int attempt = 0; attempt < 3 && !worker.cancelled(); ++attempt)
    {
        const auto response = worker.request(juce::URL(requestUrl),
            "Authorization: Bearer " + accessToken + "\r\n", "GET");
        jsonResult = juce::JSON::parse(response.body);
        const auto apiError = jsonResult["error"];
        if (response.status >= 200 && response.status < 300
            && jsonResult.isObject() && apiError.isVoid())
        {
            worker.lastError = {};
            return true;
        }

        RequestError error;
        error.httpStatus = response.status;
        error.details = apiError;
        if (const auto* reasons = apiError["errors"].getArray())
            for (const auto& reason : *reasons)
            {
                const auto name = reason["reason"].toString();
                if (error.reason.isNotEmpty())
                    error.reason += ",";
                error.reason += name;
            }
        if (const auto* details = apiError["details"].getArray())
            for (const auto& detail : *details)
            {
                const auto reason = detail["reason"].toString();
                if (reason.isNotEmpty())
                    error.reason += (error.reason.isEmpty() ? "" : ",") + reason;
            }
        if (error.reason.isEmpty())
            error.reason = apiError["status"].toString();
        const bool rateLimited = response.status == 429
            || error.reason.contains("userRateLimitExceeded")
            || error.reason.contains("rateLimitExceeded");
        const bool quotaLimited = rateLimited || error.reason.contains("dailyLimitExceeded")
            || error.reason.contains("quotaExceeded")
            || error.reason.contains("QUOTA_EXCEEDED")
            || error.reason.contains("RESOURCE_EXHAUSTED")
            || apiError["message"].toString().containsIgnoreCase("Total Query Cost");
        const bool retryable = rateLimited || response.status == 0
            || response.status == 500 || response.status == 502
            || response.status == 503 || response.status == 504;
        error.message = "HTTP " + juce::String(error.httpStatus)
            + (error.reason.isEmpty() ? juce::String{} : " (" + error.reason + ")")
            + ": " + (apiError.isObject() ? juce::JSON::toString(error.details, true)
                : response.status == 0 ? juce::String("Could not connect to Gmail API.")
                                      : juce::String("Gmail returned an invalid response."));
        errorMessage = error.message;
        worker.lastError = error;

        int delayMs = (1000 << attempt) + juce::Random::getSystemRandom().nextInt(500);
        const int retryAfter = response.headers.getValue("Retry-After", "0").getIntValue();
        if (retryAfter > 0)
            delayMs = juce::jmax(delayMs, juce::jmin(retryAfter, 300) * 1000);
        if (quotaLimited)
        {
            worker.quotaError = error;
            worker.cooldownUntil = juce::Time::getMillisecondCounterHiRes()
                + (attempt == 2 || !retryable ? juce::jmax(60000, delayMs) : delayMs);
        }
        if (!retryable || attempt == 2 || delayMs > 10000 || !worker.delay(delayMs))
            return false;
    }
    errorMessage = "Gmail request cancelled.";
    return false;
}

juce::String
GmailClient::getHeaderValue(
    const juce::var& headers,
    const juce::String& name)
{
    if (!headers.isArray())
    {
        return {};
    }

    for (const auto& header
         : *headers.getArray())
    {
        auto* object =
            header.getDynamicObject();

        if (object == nullptr)
        {
            continue;
        }

        const auto headerName =
            object
                ->getProperty(
                    "name")
                .toString();

        if (headerName
                .equalsIgnoreCase(
                    name))
        {
            return object
                ->getProperty(
                    "value")
                .toString();
        }
    }

    return {};
}

void GmailClient::collectAudioAttachments(
    const juce::var& part,
    const juce::String& messageId,
    const juce::String& sender,
    const juce::String& subject,
    std::vector<AudioAttachment>& output)
{
    auto* object =
        part.getDynamicObject();

    if (object == nullptr)
    {
        return;
    }

    const auto filename =
        object
            ->getProperty(
                "filename")
            .toString();

    const auto mimeType =
        object
            ->getProperty(
                "mimeType")
            .toString();

    const auto lowerFilename =
        filename.toLowerCase();

    const bool audioExtension =
        lowerFilename.endsWith(
            ".wav")
        || lowerFilename.endsWith(
            ".mp3")
        || lowerFilename.endsWith(
            ".aif")
        || lowerFilename.endsWith(
            ".aiff")
        || lowerFilename.endsWith(
            ".flac")
        || lowerFilename.endsWith(
            ".m4a")
        || lowerFilename.endsWith(
            ".ogg");

    const bool audioMime =
        mimeType
            .startsWithIgnoreCase(
                "audio/");

    const auto body =
        object
            ->getProperty(
                "body");

    juce::String attachmentId;

    if (auto* bodyObject =
            body.getDynamicObject())
    {
        attachmentId =
            bodyObject
                ->getProperty(
                    "attachmentId")
                .toString();
    }

    if (filename.isNotEmpty()
        && attachmentId.isNotEmpty()
        && (audioExtension
            || audioMime))
    {
        AudioAttachment item;

        item.messageId =
            messageId;

        item.attachmentId =
            attachmentId;

        item.filename =
            filename;

        item.sender =
            sender;

        item.subject =
            subject;

        output.push_back(
            std::move(item));
    }

    const auto parts =
        object
            ->getProperty(
                "parts");

    if (!parts.isArray())
    {
        return;
    }

    for (const auto& child
         : *parts.getArray())
    {
        collectAudioAttachments(
            child,
            messageId,
            sender,
            subject,
            output);
    }
}

void GmailClient::restoreLibrary(LibraryCallback callback)
{
    const auto session = getSessionGeneration();
    worker->enqueue({ {}, Priority::preview, session,
        [guard = lifetime, session, callback = std::move(callback)](Worker& background)
        {
            background.loadIndex();
            LibraryUpdate update;
            update.account = background.index.account;
            update.attachments = background.index.attachments();
            juce::MessageManager::callAsync([guard, session, callback, update]
            {
                if (guard->alive.load() && guard->generation.load() == session && callback)
                    callback(update);
            });
        }, {} });
}

void GmailClient::fetchRecentAudioAttachments(int maxMessages, LibraryCallback callback)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (!callback || librarySyncActive)
        return;
    const auto token = accessToken.isNotEmpty() ? accessToken : unverifiedAccessToken;
    if (state != State::connected || token.isEmpty())
    {
        LibraryUpdate update;
        update.error = "Gmail is not connected.";
        callback(update);
        return;
    }
    librarySyncActive = true;
    const auto session = getSessionGeneration();
    worker->enqueue({ {}, Priority::download, session,
        [this, guard = lifetime, session, token, maxMessages,
         callback = std::move(callback)](Worker& background)
        {
            const auto update = fetchRecentAudioAttachmentsSync(maxMessages, token, background);
            juce::MessageManager::callAsync([this, guard, session, token, callback, update,
                                            details = background.lastError]
            {
                if (!guard->alive.load() || guard->generation.load() != session)
                    return;
                librarySyncActive = false;
                lastRequestError = details;
                if (update.accountVerified)
                {
                    accessToken = token;
                    unverifiedAccessToken.clear();
                }
                callback(update);
            });
        }, {} });
}

GmailClient::LibraryUpdate GmailClient::fetchRecentAudioAttachmentsSync(
    int maxMessages, const juce::String& accessToken, Worker& worker)
{
    worker.loadIndex();
    bool verified = false;
    const auto finish = [&](const juce::String& error)
    {
        LibraryUpdate update;
        update.account = worker.index.account;
        update.attachments = worker.index.attachments();
        update.accountVerified = verified;
        update.error = error;
        if (error.isNotEmpty())
        {
            const double remaining = worker.cooldownUntil - juce::Time::getMillisecondCounterHiRes();
            update.nextSyncDelayMs = remaining > 0.0 ? static_cast<int>(remaining) + 250 : 60000;
        }
        return update;
    };
    if (accessToken.isEmpty())
        return finish("Gmail is not connected.");

    juce::String error;
    if (worker.verifiedGeneration != worker.runningGeneration)
    {
        juce::var profile;
        if (!performAuthorizedGet(juce::String(gmailApiBase) + "/profile",
                                  profile, error, accessToken, worker))
            return finish(error);
        const auto account = profile["emailAddress"].toString().trim().toLowerCase();
        if (account.isEmpty())
            return finish("Gmail profile did not identify the account.");
        if (worker.index.account != account)
        {
            // This small index represents the last authenticated account only.
            // Never carry known message IDs or rows into another account.
            worker.index = {};
            worker.index.account = account;
        }
        worker.verifiedGeneration = worker.runningGeneration;
    }
    verified = true;
    if (!worker.index.save(worker.indexFile))
        return finish("Could not save the Gmail discovery index.");

    maxMessages = juce::jlimit(1, 100, maxMessages);
    const auto listUrl = juce::String(gmailApiBase) + "/messages?maxResults="
        + juce::String(maxMessages) + "&q=" + urlEncode("has:attachment");
    juce::var listJson;
    if (!performAuthorizedGet(listUrl, listJson, error, accessToken, worker))
        return finish(error);
    const auto messages = listJson["messages"];
    if (messages.isVoid())
        return finish({});
    if (!messages.isArray())
        return finish("Gmail returned an invalid message list.");

    for (const auto& reference : *messages.getArray())
    {
        if (worker.cancelled())
            return finish("Gmail synchronization cancelled.");
        worker.runUrgentAttachments();
        const auto id = reference["id"].toString();
        if (id.isEmpty() || worker.index.knows(id))
            continue;
        juce::var messageJson;
        if (!performAuthorizedGet(juce::String(gmailApiBase) + "/messages/"
                                  + urlEncode(id) + "?format=full",
                                  messageJson, error, accessToken, worker))
            return finish(error);
        const auto payload = messageJson["payload"];
        if (!payload.isObject())
            return finish("Gmail returned an invalid message payload.");
        const auto headers = payload["headers"];
        DiscoveryIndex::Message message { id, getHeaderValue(headers, "From"),
                                         getHeaderValue(headers, "Subject"), {} };
        collectAudioAttachments(payload, id, message.sender, message.subject, message.attachments);
        worker.index.messages.push_back(std::move(message));
        // Checkpoint even messages with no audio; a later HTTP failure or exit
        // must not discard successful discovery or repeat it on the next launch.
        if (!worker.index.save(worker.indexFile))
            return finish("Could not save the Gmail discovery index.");
    }
    return finish({});
}
void GmailClient::
    downloadAudioAttachment(
        const juce::String& messageId,
        const juce::String& attachmentId,
        const juce::String& filename,
        const juce::File& destinationDirectory,
        DownloadCallback callback, Priority priority, const juce::File& localFile)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (!callback)
        return;
    // Local cache reads are cheap and must not wait behind a running HTTP request.
    const auto diskFile = getAttachmentCacheFile(messageId, attachmentId, filename, destinationDirectory);
    const auto available = localFile.existsAsFile() && localFile.getSize() > 0 ? localFile : diskFile;
    if (available.existsAsFile() && available.getSize() > 0)
    {
        lastRequestError = {};
        callback(available, {});
        return;
    }
    const auto key = messageId + "|" + attachmentId;
    const auto pending = pendingAttachments.find(key);
    if (pending != pendingAttachments.end())
    {
        pending->second->callbacks.push_back(std::move(callback));
        worker->promote(key, priority);
        return;
    }
    auto request = std::make_shared<PendingAttachment>();
    request->callbacks.push_back(std::move(callback));
    pendingAttachments[key] = request;
    const auto session = getSessionGeneration();
    const auto complete = [this, guard = lifetime, session, key, request](
        const juce::File& file, const juce::String& error, const RequestError& details = {})
    {
        juce::MessageManager::callAsync([this, guard, session, key, request, file, error, details]
        {
            if (!guard->alive.load() || guard->generation.load() != session)
                return;
            const auto current = pendingAttachments.find(key);
            if (current == pendingAttachments.end() || current->second != request)
                return;
            pendingAttachments.erase(current);
            lastRequestError = details;
            for (const auto& consumer : request->callbacks)
            {
                if (!guard->alive.load() || guard->generation.load() != session)
                    break;
                consumer(file, error);
            }
        });
    };
    worker->enqueue({ key, priority, session,
        [token = state == State::connected ? accessToken : juce::String{},
         messageId, attachmentId, filename, destinationDirectory,
         localFile, complete](Worker& background)
        {
            const auto diskFile = getAttachmentCacheFile(messageId, attachmentId, filename,
                                                         destinationDirectory);
            if (localFile.existsAsFile() && localFile.getSize() > 0)
                complete(localFile, {});
            else if (diskFile.existsAsFile() && diskFile.getSize() > 0)
                complete(diskFile, {});
            else
                downloadAudioAttachmentSync(messageId, attachmentId, filename,
                    destinationDirectory,
                    [complete, &background](const juce::File& file, const juce::String& error)
                        { complete(file, error, background.lastError); }, token, background);
        }, [complete] { complete({}, "Gmail prefetch cancelled."); } });
}

void GmailClient::discardQueuedNavigation(const juce::String& keepKey)
{
    for (const auto& key : worker->discardNavigation(keepKey))
        pendingAttachments.erase(key);
}

void GmailClient::saveAttachment(const juce::File& file, const juce::File& directory,
                                 DownloadCallback callback)
{
    const auto session = getSessionGeneration();
    worker->enqueue({ {}, Priority::download, session,
        [guard = lifetime, session, file, directory, callback](Worker& background)
        {
            const auto target = directory.getChildFile(file.getFileName());
            // Browser/drag discovery scans this directory for files. Keep the
            // unfinished copy in a subdirectory until it is ready to publish.
            const auto staging = directory.getChildFile(".pending");
            juce::TemporaryFile temporary(target, staging.getChildFile(juce::Uuid().toString()));
            const bool saved = !background.cancelled() && staging.createDirectory().wasOk()
                && file.copyFileTo(temporary.getFile())
                && temporary.overwriteTargetFileWithTemporary();
            juce::MessageManager::callAsync([guard, session, callback, target, saved]
            {
                if (guard->alive.load() && guard->generation.load() == session)
                    callback(saved ? target : juce::File{},
                             saved ? juce::String{} : juce::String("Could not save file."));
            });
        }, {} });
}

juce::File GmailClient::getAttachmentCacheFile(
    const juce::String& messageId, const juce::String& attachmentId,
    const juce::String& filename, const juce::File& directory)
{
    const auto identity = messageId + "|" + attachmentId;
    return directory.getChildFile(juce::SHA256(identity.toRawUTF8(),
        identity.getNumBytesAsUTF8()).toHexString() + "_" + sanitiseFilename(filename));
}

void GmailClient::downloadAudioAttachmentSync(
    const juce::String& messageId, const juce::String& attachmentId,
    const juce::String& filename, const juce::File& destinationDirectory,
    DownloadCallback callback, const juce::String& accessToken, Worker& worker)
{
    if (!callback)
    {
        return;
    }

    if (accessToken.isEmpty())
    {
        callback(
            {},
            "Gmail is not connected.");

        return;
    }

    if (messageId.isEmpty()
        || attachmentId.isEmpty())
    {
        callback(
            {},
            "The selected Gmail attachment "
            "does not have a valid ID.");

        return;
    }

    auto directory =
        destinationDirectory;

    if (!directory.exists())
    {
        const auto result =
            directory.createDirectory();

        if (result.failed())
        {
            callback(
                {},
                "Could not create the "
                "LoopBridge cache directory: "
                + result.getErrorMessage());

            return;
        }
    }

    if (!directory.isDirectory())
    {
        callback(
            {},
            "The LoopBridge cache path "
            "is not a directory.");

        return;
    }

    const auto requestUrl =
        juce::String(
            gmailApiBase)
        + "/messages/"
        + urlEncode(
            messageId)
        + "/attachments/"
        + urlEncode(
            attachmentId);

    juce::var json;
    juce::String error;

    if (!performAuthorizedGet(
            requestUrl,
            json,
            error, accessToken, worker))
    {
        callback(
            {},
            error);

        return;
    }

    auto* object =
        json.getDynamicObject();

    if (object == nullptr)
    {
        callback(
            {},
            "Gmail returned an invalid "
            "attachment response.");

        return;
    }

    const auto encodedData =
        object
            ->getProperty(
                "data")
            .toString();

    if (encodedData.isEmpty())
    {
        callback(
            {},
            "Gmail returned an empty "
            "audio attachment.");

        return;
    }

    juce::MemoryBlock decodedData;

    if (!decodeBase64Url(
            encodedData,
            decodedData))
    {
        callback(
            {},
            "Could not decode the Gmail "
            "audio attachment.");

        return;
    }

    if (decodedData.getSize() == 0)
    {
        callback(
            {},
            "The decoded Gmail attachment "
            "was empty.");

        return;
    }

    const auto outputFile = getAttachmentCacheFile(messageId, attachmentId, filename, directory);
    // Publish only complete files so interrupted writes cannot become cache hits.
    juce::TemporaryFile temporary(outputFile);

    std::unique_ptr<
        juce::FileOutputStream>
        outputStream(
            temporary.getFile()
                .createOutputStream());

    if (outputStream == nullptr)
    {
        callback(
            {},
            "Could not create the cached "
            "audio file.");

        return;
    }

    const bool writeSucceeded =
        outputStream->write(
            decodedData.getData(),
            decodedData.getSize());

    outputStream->flush();

    outputStream.reset();

    if (!writeSucceeded
        || temporary.getFile().getSize() <= 0
        || !temporary.overwriteTargetFileWithTemporary())
    {

        callback(
            {},
            "Could not write the Gmail "
            "audio attachment to disk.");

        return;
    }

    callback(
        outputFile,
        {});
}

void GmailClient::setState(
    State newState,
    const juce::String& message)
{
    state =
        newState;

    statusText =
        message;

    if (stateCallback)
    {
        stateCallback(
            state,
            statusText);
    }
}

juce::String
GmailClient::base64UrlEncode(
    const void* data,
    size_t size)
{
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789-_";

    const auto* bytes =
        static_cast<const unsigned char*>(
            data);

    juce::String result;

    for (size_t i = 0; i < size; i += 3)
    {
        const unsigned int byte0 =
            bytes[i];

        const unsigned int byte1 =
            (i + 1 < size)
                ? bytes[i + 1]
                : 0;

        const unsigned int byte2 =
            (i + 2 < size)
                ? bytes[i + 2]
                : 0;

        const unsigned int value =
            (byte0 << 16)
            | (byte1 << 8)
            | byte2;

        result += alphabet[
            (value >> 18) & 0x3f];

        result += alphabet[
            (value >> 12) & 0x3f];

        if (i + 1 < size)
        {
            result += alphabet[
                (value >> 6) & 0x3f];
        }

        if (i + 2 < size)
        {
            result += alphabet[
                value & 0x3f];
        }
    }

    return result;
}

#if defined(LOOPBRIDGE_GMAIL_TESTS)
int runGmailClientTests()
{
    using Worker = GmailClient::Worker;
    using Priority = GmailClient::Priority;
    std::atomic<int> failures { 0 };
    const auto check = [&failures](bool success, const char* name)
    {
        if (!success)
        {
            ++failures;
            std::cout << "FAIL: " << name << '\n';
        }
    };
    const auto pumpUntil = [](const std::function<bool()>& complete)
    {
        const double deadline = juce::Time::getMillisecondCounterHiRes() + 8000.0;
        while (!complete() && juce::Time::getMillisecondCounterHiRes() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        return complete();
    };
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("LoopBridgeGmailTests-" + juce::Uuid().toString());
    check(directory.createDirectory().wasOk(), "test directory");

    {
        auto lifetime = std::make_shared<GmailClient::Lifetime>();
        juce::WaitableEvent started, release, finished;
        std::vector<juce::String> order;
        Worker worker(lifetime);
        worker.enqueue({ {}, Priority::preview, 0, [&](Worker& background)
        {
            started.signal();
            while (!background.cancelled() && !release.wait(10)) {}
        }, {} });
        check(started.wait(2000), "worker starts independently");
        const auto add = [&](const char* key, Priority priority)
        {
            worker.enqueue({ key, priority, 0, [&, key](Worker&)
            {
                order.push_back(key);
                if (order.size() == 3)
                    finished.signal();
            }, {} });
        };
        add("prefetch", Priority::prefetch);
        add("download", Priority::download);
        add("retained", Priority::prefetch);
        worker.promote("retained", Priority::download);
        worker.promote("retained", Priority::preview);
        add("preview", Priority::prefetch);
        worker.promote("preview", Priority::preview);
        const auto discarded = worker.discardNavigation("preview");
        check(discarded.size() == 1 && discarded.front() == "prefetch",
              "discard speculation while retaining explicit downloads");
        release.signal();
        check(finished.wait(2000), "queued work finishes");
        worker.signalThreadShouldExit();
        worker.notify();
        worker.stopThread(-1);
        check(order == std::vector<juce::String>{ "preview", "download", "retained" },
              "preview priority, download priority, and promotion");
    }

    {
        juce::WaitableEvent started, release;
        GmailClient client;
        client.state = GmailClient::State::connected;
        client.accessToken = "offline-test-token";
        std::atomic<int> requests { 0 };
        std::atomic<bool> offMessageThread { true };
        client.worker->enqueue({ {}, Priority::preview, client.getSessionGeneration(),
            [&](Worker& background)
            {
                started.signal();
                while (!background.cancelled() && !release.wait(10)) {}
            }, {} });
        check(started.wait(2000), "acquisition worker gate");
        client.worker->testRequest = [&](const juce::URL&)
        {
            ++requests;
            if (juce::MessageManager::getInstance()->isThisTheMessageThread())
                offMessageThread.store(false);
            return Worker::Response { 200, {}, "{\"data\":\"dGVzdA\"}" };
        };
        int completed = 0;
        const auto callback = [&](const juce::File& file, const juce::String& error)
        {
            check(juce::MessageManager::getInstance()->isThisTheMessageThread(), "message-thread callback");
            check(error.isEmpty() && file.getSize() == 4, "decoded and published cache file");
            ++completed;
        };
        client.downloadAudioAttachment("message", "first", "same.wav", directory, callback, Priority::prefetch);
        client.downloadAudioAttachment("message", "first", "same.wav", directory, callback, Priority::download);
        client.downloadAudioAttachment("message", "first", "same.wav", directory, callback, Priority::preview);
        check(client.pendingAttachments.size() == 1, "one shared pending acquisition");
        release.signal();
        check(pumpUntil([&] { return completed == 3; }), "all acquisition consumers complete");
        check(requests.load() == 1 && offMessageThread.load(), "one background HTTP request");
        client.downloadAudioAttachment("message", "second", "same.wav", directory, callback);
        check(pumpUntil([&] { return completed == 4; }), "second same-named attachment");
        const auto first = GmailClient::getAttachmentCacheFile("message", "first", "same.wav", directory);
        const auto second = GmailClient::getAttachmentCacheFile("message", "second", "same.wav", directory);
        check(first != second && first.existsAsFile() && second.existsAsFile(), "unique attachment files");
        client.downloadAudioAttachment("message", "first", "same.wav", directory, callback);
        client.downloadAudioAttachment("other", "saved", "same.wav", directory, callback,
                                       Priority::preview, first);
        check(completed == 6 && requests.load() == 2, "disk and supplied permanent cache avoid HTTP");
        bool saved = false;
        client.saveAttachment(first, directory.getChildFile("saved"),
            [&](const juce::File& file, const juce::String& error)
            { saved = error.isEmpty() && file.getSize() == 4; });
        check(pumpUntil([&] { return saved; }), "background permanent save");
    }

    {
        juce::WaitableEvent started, release;
        GmailClient client;
        client.state = GmailClient::State::connected;
        client.accessToken = "offline-test-token";
        std::atomic<int> requests { 0 };
        client.worker->enqueue({ {}, Priority::preview, client.getSessionGeneration(),
            [&](Worker& background)
            {
                started.signal();
                while (!background.cancelled() && !release.wait(10)) {}
            }, {} });
        check(started.wait(2000), "cancellation worker gate");
        client.worker->testRequest = [&](const juce::URL&)
        { ++requests; return Worker::Response { 200, {}, "{\"data\":\"dGVzdA\"}" }; };
        int obsolete = 0, current = 0;
        client.downloadAudioAttachment("cancel", "first", "same.wav", directory,
            [&](const juce::File&, const juce::String&) { ++obsolete; }, Priority::prefetch);
        client.discardQueuedNavigation();
        client.downloadAudioAttachment("cancel", "first", "same.wav", directory,
            [&](const juce::File&, const juce::String& error) { if (error.isEmpty()) ++current; });
        release.signal();
        check(pumpUntil([&] { return current == 1; }), "demand after cancelled prefetch succeeds");
        check(obsolete == 0 && requests.load() == 1, "late cancellation cannot erase new demand");

        juce::WaitableEvent running, finish;
        client.worker->enqueue({ {}, Priority::preview, client.getSessionGeneration(),
            [&](Worker& background)
            {
                running.signal();
                while (!background.cancelled() && !finish.wait(10)) {}
            }, {} });
        check(running.wait(2000), "session worker gate");
        client.downloadAudioAttachment("session", "old", "same.wav", directory,
            [&](const juce::File&, const juce::String&) { ++obsolete; });
        client.disconnect();
        client.state = GmailClient::State::connected;
        client.accessToken = "new-offline-token";
        client.downloadAudioAttachment("session", "new", "same.wav", directory,
            [&](const juce::File&, const juce::String& error) { if (error.isEmpty()) ++current; });
        finish.signal();
        check(pumpUntil([&] { return current == 2; }), "new session succeeds");
        check(obsolete == 0, "old session callback suppressed");
    }

    {
        juce::WaitableEvent started, release;
        GmailClient client;
        client.state = GmailClient::State::connected;
        client.accessToken = "offline-test-token";
        std::atomic<int> requests { 0 };
        client.worker->testRequest = [&](const juce::URL&)
        {
            ++requests;
            started.signal();
            release.wait(2000);
            return Worker::Response { 200, {}, "{\"data\":\"dGVzdA\"}" };
        };
        int oldCallbacks = 0, newCallbacks = 0;
        client.downloadAudioAttachment("running", "attachment", "same.wav", directory,
            [&](const juce::File&, const juce::String&) { ++oldCallbacks; });
        check(started.wait(2000), "attachment transfer is running");
        client.downloadAudioAttachment("running", "attachment", "same.wav", directory,
            [&](const juce::File&, const juce::String&) { ++oldCallbacks; }, Priority::download);
        check(client.pendingAttachments.size() == 1, "running transfer deduplicates additional consumers");
        client.disconnect();
        client.state = GmailClient::State::connected;
        client.accessToken = "new-offline-token";
        client.downloadAudioAttachment("running", "attachment", "same.wav", directory,
            [&](const juce::File& file, const juce::String& error)
            { if (error.isEmpty() && file.getSize() == 4) ++newCallbacks; });
        release.signal();
        check(pumpUntil([&] { return newCallbacks == 1; }), "new session reuses completed old-session cache");
        check(oldCallbacks == 0 && requests.load() == 1,
              "running transfer finishes caching without stale callbacks or duplicate HTTP");
    }

    {
        auto lifetime = std::make_shared<GmailClient::Lifetime>();
        juce::WaitableEvent started, release;
        Worker worker(lifetime);
        worker.enqueue({ {}, Priority::preview, 0, [&](Worker& background)
        {
            started.signal();
            while (!background.cancelled() && !release.wait(10)) {}
        }, {} });
        check(started.wait(2000), "error worker gate");
        int requests = 0;
        worker.testRequest = [&](const juce::URL&)
        {
            ++requests;
            return Worker::Response { 403, {},
                "{\"error\":{\"errors\":[{\"reason\":\"domainPolicy\"}],\"message\":\"Forbidden\"}}" };
        };
        std::atomic<bool> done { false };
        worker.enqueue({ {}, Priority::preview, 0, [&](Worker& background)
        {
            juce::var json;
            juce::String error;
            check(!GmailClient::performAuthorizedGet("https://offline.test", json, error, "test", background),
                  "permission failure reported");
            check(requests == 1 && background.lastError.httpStatus == 403
                && background.lastError.reason == "domainPolicy", "403 permissions not retried; structured error retained");
            worker.testRequest = [&](const juce::URL&)
            {
                ++requests;
                return Worker::Response { 403, {},
                    "{\"error\":{\"errors\":[{\"reason\":\"dailyLimitExceeded\"}],\"message\":\"Quota\"}}" };
            };
            GmailClient::performAuthorizedGet("https://offline.test", json, error, "test", background);
            GmailClient::performAuthorizedGet("https://offline.test", json, error, "test", background);
            check(requests == 2 && error.contains("cooldown"), "quota cooldown prevents subsequent HTTP");
            background.cooldownUntil = 0;
            int attempts = 0;
            worker.testRequest = [&](const juce::URL&)
            {
                ++attempts;
                return attempts < 3 ? Worker::Response { 503, {}, "{}" }
                                    : Worker::Response { 200, {}, "{}" };
            };
            check(GmailClient::performAuthorizedGet("https://offline.test", json, error, "test", background)
                && attempts == 3, "bounded backoff recovers retryable failure");
            done.store(true);
        }, {} });
        release.signal();
        check(pumpUntil([&] { return done.load(); }), "error handling completes");
    }

    {
        GmailClient::DiscoveryIndex index;
        index.account = "first@example.com";
        index.messages.push_back({ "known", "Producer <producer@example.com>", "Loops", {
            { "known", "wav", "beat_120_Cm.wav", "Producer <producer@example.com>", "Loops" },
            { "known", "flac", "melody.flac", "Producer <producer@example.com>", "Loops" } } });
        index.messages.push_back({ "non-audio", "Sender", "PDF only", {} });
        const auto file = directory.getChildFile("roundtrip.json");
        check(index.save(file), "atomic discovery index save");
        GmailClient::DiscoveryIndex restored;
        check(restored.load(file) && restored.account == index.account
              && restored.knows("non-audio") && restored.messages.size() == 2,
              "account and non-audio messages survive restart");
        const auto rows = restored.attachments();
        check(rows.size() == 2 && rows[0].messageId == "known" && rows[0].attachmentId == "wav"
              && rows[0].filename == "beat_120_Cm.wav" && rows[0].sender == index.messages[0].sender
              && rows[0].subject == "Loops", "attachment identity and row metadata roundtrip");
        index.messages.push_back({ "later", "Sender", "No audio", {} });
        check(index.save(file) && restored.load(file) && restored.knows("later"),
              "replacement updates an existing index");
        const auto corrupt = directory.getChildFile("corrupt.json");
        check(corrupt.replaceWithText("{\"version\":1,\"account\":\"other@example.com\",\"messages\":[{}]}"),
              "corrupt fixture written");
        check(!restored.load(corrupt) && restored.account == "first@example.com"
              && restored.knows("known"), "malformed index cannot partially replace valid state");
    }

    {
        const auto file = directory.getChildFile("incremental.json");
        GmailClient::DiscoveryIndex seed;
        seed.account = "first@example.com";
        seed.messages.push_back({ "known", "Sender", "Old loop", {
            { "known", "old-audio", "old.wav", "Sender", "Old loop" } } });
        seed.messages.push_back({ "non-audio", "Sender", "PDF", {} });
        check(seed.save(file), "incremental fixture saved");
        GmailClient client(file);
        client.state = GmailClient::State::connected;
        client.unverifiedAccessToken = "offline-test-token";
        std::atomic<int> profiles { 0 }, lists { 0 }, details { 0 };
        client.worker->testRequest = [&](const juce::URL& url)
        {
            check(!juce::MessageManager::getInstance()->isThisTheMessageThread(),
                  "discovery HTTP runs off the UI thread");
            const auto address = url.toString(true);
            if (address.endsWith("/profile"))
            {
                ++profiles;
                return Worker::Response { 200, {}, R"({"emailAddress":"FIRST@example.com"})" };
            }
            if (address.contains("/messages?"))
            {
                ++lists;
                check(address.contains("maxResults=100") && address.contains("has%3Aattachment"),
                      "bounded discovery query preserved");
                return Worker::Response { 200, {},
                    R"({"messages":[{"id":"known"},{"id":"non-audio"},{"id":"new"}]})" };
            }
            ++details;
            check(address.contains("/messages/new?format=full"), "only unknown message fetched");
            return Worker::Response { 200, {},
                R"({"payload":{"headers":[{"name":"From","value":"New Producer"},{"name":"Subject","value":"New loops"}],"parts":[{"filename":"new.wav","mimeType":"audio/wav","body":{"attachmentId":"new-audio"}}]}})" };
        };
        bool restored = false;
        client.restoreLibrary([&](const GmailClient::LibraryUpdate& update)
        {
            check(update.account == "first@example.com" && update.attachments.size() == 1
                  && profiles == 0 && lists == 0, "startup restores known rows without Gmail");
            restored = true;
        });
        check(pumpUntil([&] { return restored; }), "startup restoration completes");
        int completed = 0;
        const auto callback = [&](const GmailClient::LibraryUpdate& update)
        {
            check(update.error.isEmpty() && update.accountVerified && update.attachments.size() == 2
                  && update.nextSyncDelayMs == 120000, "incremental snapshot and modest polling");
            check(client.accessToken == "offline-test-token", "attachment token enabled after account verification");
            ++completed;
        };
        client.fetchRecentAudioAttachments(100, callback);
        client.fetchRecentAudioAttachments(100, callback);
        check(client.isLibrarySyncActive(), "single-flight sync guard set");
        check(pumpUntil([&] { return completed == 1; }) && !client.isLibrarySyncActive(),
              "one concurrent synchronization and completion");
        client.fetchRecentAudioAttachments(100, callback);
        check(pumpUntil([&] { return completed == 2; }), "second poll completes");
        check(profiles == 1 && lists == 2 && details == 1,
              "profile once per session, known audio and non-audio messages skipped");
        GmailClient::DiscoveryIndex disk;
        check(disk.load(file) && disk.knows("new") && disk.knows("non-audio"),
              "new discovery persisted for the next process");
    }

    {
        const auto file = directory.getChildFile("partial.json");
        GmailClient::DiscoveryIndex seed;
        seed.account = "first@example.com";
        seed.messages.push_back({ "known", "Sender", "Old", {
            { "known", "audio", "old.wav", "Sender", "Old" } } });
        check(seed.save(file), "partial-sync fixture saved");
        {
            GmailClient client(file);
            client.state = GmailClient::State::connected;
            client.unverifiedAccessToken = "offline-test-token";
            client.worker->testRequest = [&](const juce::URL& url)
            {
                const auto address = url.toString(true);
                if (address.endsWith("/profile"))
                    return Worker::Response { 200, {}, R"({"emailAddress":"first@example.com"})" };
                if (address.contains("/messages?"))
                    return Worker::Response { 200, {}, R"({"messages":[{"id":"fresh"},{"id":"empty"},{"id":"failure"}]})" };
                if (address.contains("/messages/fresh?"))
                    return Worker::Response { 200, {},
                        R"({"payload":{"parts":[{"filename":"fresh.wav","body":{"attachmentId":"fresh-audio"}}]}})" };
                if (address.contains("/messages/empty?"))
                    return Worker::Response { 200, {}, R"({"payload":{"parts":[]}})" };
                return Worker::Response { 403, {},
                    R"({"error":{"errors":[{"reason":"dailyLimitExceeded"}],"message":"Total Query Cost"}})" };
            };
            bool completed = false;
            client.fetchRecentAudioAttachments(100, [&](const GmailClient::LibraryUpdate& update)
            {
                check(update.error.isNotEmpty() && update.attachments.size() == 2
                      && update.nextSyncDelayMs >= 59000, "partial library retained and retry follows cooldown");
                completed = true;
            });
            check(pumpUntil([&] { return completed; }), "partial failure completes");
        }
        GmailClient::DiscoveryIndex disk;
        check(disk.load(file) && disk.knows("known") && disk.knows("fresh") && disk.knows("empty")
              && !disk.knows("failure"), "successful partial progress including non-audio survives shutdown");
        GmailClient restarted(file);
        restarted.state = GmailClient::State::connected;
        restarted.unverifiedAccessToken = "offline-test-token";
        std::atomic<int> details { 0 };
        restarted.worker->testRequest = [&](const juce::URL& url)
        {
            const auto address = url.toString(true);
            if (address.endsWith("/profile"))
                return Worker::Response { 200, {}, R"({"emailAddress":"first@example.com"})" };
            if (address.contains("/messages?"))
                return Worker::Response { 200, {},
                    R"({"messages":[{"id":"known"},{"id":"fresh"},{"id":"empty"},{"id":"failure"}]})" };
            ++details;
            check(address.contains("/messages/failure?"), "restart fetches only the previously failed message");
            return Worker::Response { 200, {}, R"({"payload":{}})" };
        };
        bool completed = false;
        restarted.fetchRecentAudioAttachments(100, [&](const GmailClient::LibraryUpdate& update)
        {
            check(update.error.isEmpty() && update.attachments.size() == 2, "partial-sync recovery retains library");
            completed = true;
        });
        check(pumpUntil([&] { return completed; }) && details == 1, "partial progress reused after restart");
    }

    {
        const auto file = directory.getChildFile("accounts.json");
        GmailClient::DiscoveryIndex seed;
        seed.account = "first@example.com";
        seed.messages.push_back({ "shared-id", "First", "Old account", {
            { "shared-id", "old", "old.wav", "First", "Old account" } } });
        check(seed.save(file), "account separation fixture saved");
        GmailClient client(file);
        client.state = GmailClient::State::connected;
        client.unverifiedAccessToken = "second-account-token";
        std::atomic<int> requests { 0 }, details { 0 };
        bool profileUnavailable = true;
        client.worker->testRequest = [&](const juce::URL& url)
        {
            ++requests;
            const auto address = url.toString(true);
            if (address.endsWith("/profile"))
                return profileUnavailable ? Worker::Response { 403, {},
                    R"({"error":{"errors":[{"reason":"dailyLimitExceeded"}],"message":"Total Query Cost"}})" }
                    : Worker::Response { 200, {}, R"({"emailAddress":"second@example.com"})" };
            if (address.contains("/messages?"))
                return Worker::Response { 200, {}, R"({"messages":[{"id":"shared-id"}]})" };
            ++details;
            return Worker::Response { 200, {},
                R"({"payload":{"parts":[{"filename":"second.wav","body":{"attachmentId":"second"}}]}})" };
        };
        int completed = 0;
        const auto paused = [&](const GmailClient::LibraryUpdate& update)
        {
            check(!update.accountVerified && update.account == "first@example.com"
                  && update.attachments.size() == 1 && update.nextSyncDelayMs >= 59000
                  && client.accessToken.isEmpty(), "unverified account keeps offline rows without enabling uncached audio");
            ++completed;
        };
        client.fetchRecentAudioAttachments(100, paused);
        check(pumpUntil([&] { return completed == 1; }), "profile quota failure completes");
        client.fetchRecentAudioAttachments(100, paused);
        check(pumpUntil([&] { return completed == 2; }) && requests == 1,
              "cooldown prevents profile and discovery HTTP on early retry");
        // Change worker state on its own thread, without waiting a real minute.
        client.worker->enqueue({ {}, Priority::preview, client.getSessionGeneration(), [&](Worker& background)
        {
            background.cooldownUntil = 0;
            profileUnavailable = false;
        }, {} });
        client.fetchRecentAudioAttachments(100, [&](const GmailClient::LibraryUpdate& update)
        {
            check(update.accountVerified && update.account == "second@example.com"
                  && update.attachments.size() == 1 && update.attachments[0].filename == "second.wav",
                  "authenticated account switch replaces rather than merges rows");
            ++completed;
        });
        check(pumpUntil([&] { return completed == 3; }) && details == 1,
              "known IDs from another account do not suppress discovery");
        GmailClient::DiscoveryIndex disk;
        check(disk.load(file) && disk.account == "second@example.com"
              && disk.attachments().size() == 1 && disk.attachments()[0].attachmentId == "second",
              "persisted account metadata contains no old-account attachments");
    }

    {
        int callbacks = 0;
        auto client = std::make_unique<GmailClient>();
        client->downloadAudioAttachment("shutdown", "missing", "same.wav", directory,
            [&](const juce::File&, const juce::String&) { ++callbacks; });
        client.reset();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        check(callbacks == 0, "shutdown suppresses queued callbacks");
    }
    directory.deleteRecursively();
    return failures.load();
}
#endif
