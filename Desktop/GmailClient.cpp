#include "GmailClient.h"

#include <cstring>
#include <random>
#include <string>
#include <vector>

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
        SOCKET socketHandle)
    {
        std::string request;

        char buffer[2048];

        while (
            request.find("\r\n\r\n")
            == std::string::npos)
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
          port(portIn)
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

        fd_set readSet;

        FD_ZERO(
            &readSet);

        FD_SET(
            serverSocket,
            &readSet);

        timeval timeout {};

        timeout.tv_sec = 180;
        timeout.tv_usec = 0;

        const int ready =
            select(
                0,
                &readSet,
                nullptr,
                nullptr,
                &timeout);

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

        const auto request =
            readHttpRequest(
                clientSocket);

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
                == owner.stateToken;

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
            != owner.stateToken)
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
            [this, code]
            {
                owner
                    .handleAuthorizationCode(
                        code);
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
            [this, message]
            {
                owner.setState(
                    GmailClient::State::error,
                    message);
            });
    }

    GmailClient& owner;

    int port = 0;
};

GmailClient::GmailClient() =
    default;

GmailClient::~GmailClient()
{
    if (authThread != nullptr)
    {
        authThread
            ->signalThreadShouldExit();

        authThread
            ->stopThread(2000);
    }
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
    if (authThread != nullptr)
    {
        authThread
            ->signalThreadShouldExit();

        authThread
            ->stopThread(1000);

        authThread.reset();
    }

    accessToken.clear();
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
            ->stopThread(1000);

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

    TokenData tokens;

    if (!exchangeCodeForTokens(
            code,
            tokens))
    {
        setState(
            State::error,
            "Could not exchange "
            "authorization code for "
            "Google tokens.");

        return;
    }

    if (tokens.accessToken.isEmpty())
    {
        setState(
            State::error,
            "Google returned an empty "
            "access token.");

        return;
    }

    accessToken =
        tokens.accessToken;

    refreshToken =
        tokens.refreshToken;

    setState(
        State::connected,
        "Connected to Gmail");
}

bool GmailClient::
    exchangeCodeForTokens(
        const juce::String& code,
        TokenData& tokens)
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

    const auto options =
        juce::URL::InputStreamOptions(
            juce::URL::
                ParameterHandling::
                    inAddress)
            .withHttpRequestCmd(
                "POST")
            .withExtraHeaders(
                "Content-Type: "
                "application/"
                "x-www-form-urlencoded"
                "\r\n")
            .withConnectionTimeoutMs(
                15000);

    auto stream =
        url.createInputStream(
            options);

    if (stream == nullptr)
    {
        return false;
    }

    const auto response =
        stream
            ->readEntireStreamAsString();

    const auto json =
        juce::JSON::parse(
            response);

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
    juce::String& errorMessage) const
{
    if (accessToken.isEmpty())
    {
        errorMessage =
            "No Gmail access token is available.";

        return false;
    }

    const auto options =
        juce::URL::InputStreamOptions(
            juce::URL::
                ParameterHandling::
                    inAddress)
            .withHttpRequestCmd(
                "GET")
            .withExtraHeaders(
                "Authorization: Bearer "
                + accessToken
                + "\r\n")
            .withConnectionTimeoutMs(
                15000);

    auto stream =
        juce::URL(
            requestUrl)
            .createInputStream(
                options);

    if (stream == nullptr)
    {
        errorMessage =
            "Could not connect to Gmail API.";

        return false;
    }

    const auto response =
        stream
            ->readEntireStreamAsString();

    jsonResult =
        juce::JSON::parse(
            response);

    if (!jsonResult.isObject())
    {
        errorMessage =
            "Gmail API returned invalid JSON.";

        return false;
    }

    if (auto* object =
            jsonResult.getDynamicObject())
    {
        const auto apiError =
            object->getProperty(
                "error");

        if (!apiError.isVoid()
            && !apiError.isUndefined())
        {
            errorMessage =
                "Gmail API returned an error: "
                + juce::JSON::toString(
                    apiError,
                    true);

            return false;
        }
    }

    return true;
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

void GmailClient::
    fetchRecentAudioAttachments(
        int maxMessages,
        AudioListCallback callback)
{
    if (!callback)
    {
        return;
    }

    if (state
            != State::connected
        || accessToken.isEmpty())
    {
        callback(
            {},
            "Gmail is not connected.");

        return;
    }

    maxMessages =
        juce::jlimit(
            1,
            100,
            maxMessages);

    const auto listUrl =
        juce::String(
            gmailApiBase)
        + "/messages?maxResults="
        + juce::String(
            maxMessages)
        + "&q="
        + urlEncode(
            "has:attachment");

    juce::var listJson;
    juce::String error;

    if (!performAuthorizedGet(
            listUrl,
            listJson,
            error))
    {
        callback(
            {},
            error);

        return;
    }

    auto* listObject =
        listJson
            .getDynamicObject();

    if (listObject == nullptr)
    {
        callback(
            {},
            "Gmail message list was empty.");

        return;
    }

    const auto messages =
        listObject
            ->getProperty(
                "messages");

    if (!messages.isArray())
    {
        callback(
            {},
            {});

        return;
    }

    std::vector<AudioAttachment>
        results;

    for (const auto& message
         : *messages.getArray())
    {
        auto* messageRef =
            message
                .getDynamicObject();

        if (messageRef == nullptr)
        {
            continue;
        }

        const auto messageId =
            messageRef
                ->getProperty(
                    "id")
                .toString();

        if (messageId.isEmpty())
        {
            continue;
        }

        const auto messageUrl =
            juce::String(
                gmailApiBase)
            + "/messages/"
            + urlEncode(
                messageId)
            + "?format=full";

        juce::var messageJson;

        if (!performAuthorizedGet(
                messageUrl,
                messageJson,
                error))
        {
            callback(
                {},
                error);

            return;
        }

        auto* messageObject =
            messageJson
                .getDynamicObject();

        if (messageObject == nullptr)
        {
            continue;
        }

        const auto payload =
            messageObject
                ->getProperty(
                    "payload");

        auto* payloadObject =
            payload
                .getDynamicObject();

        if (payloadObject == nullptr)
        {
            continue;
        }

        const auto headers =
            payloadObject
                ->getProperty(
                    "headers");

        const auto sender =
            getHeaderValue(
                headers,
                "From");

        const auto subject =
            getHeaderValue(
                headers,
                "Subject");

        collectAudioAttachments(
            payload,
            messageId,
            sender,
            subject,
            results);
    }

    callback(
        results,
        {});
}

void GmailClient::
    downloadAudioAttachment(
        const juce::String& messageId,
        const juce::String& attachmentId,
        const juce::String& filename,
        const juce::File& destinationDirectory,
        DownloadCallback callback) const
{
    if (!callback)
    {
        return;
    }

    if (state
            != State::connected
        || accessToken.isEmpty())
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
            error))
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

    const auto safeFilename =
        sanitiseFilename(
            filename);

    const auto messagePrefix =
        messageId.substring(
            0,
            juce::jmin(
                12,
                messageId.length()));

    auto outputFile =
        directory.getChildFile(
            messagePrefix
            + "_"
            + safeFilename);

    outputFile.deleteFile();

    std::unique_ptr<
        juce::FileOutputStream>
        outputStream(
            outputFile
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
        || !outputFile.existsAsFile()
        || outputFile.getSize() <= 0)
    {
        outputFile.deleteFile();

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