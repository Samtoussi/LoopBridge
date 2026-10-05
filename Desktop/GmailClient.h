#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <vector>
#include <atomic>
#include <map>

class GmailAuthThread;

class GmailClient
{
public:
    enum class State
    {
        disconnected,
        authorizing,
        connected,
        error
    };

    using StateCallback =
        std::function<void(
            State,
            const juce::String&)>;

    struct AudioAttachment
    {
        juce::String messageId;
        juce::String attachmentId;
        juce::String filename;
        juce::String sender;
        juce::String subject;
    };

    struct LibraryUpdate
    {
        juce::String account;
        std::vector<AudioAttachment> attachments;
        juce::String error;
        bool accountVerified = false;
        int nextSyncDelayMs = 120000;
    };
    using LibraryCallback = std::function<void(const LibraryUpdate&)>;

    using DownloadCallback =
        std::function<void(
            const juce::File&,
            const juce::String&)>;

    enum class Priority { preview, download, prefetch };

    struct RequestError
    {
        int httpStatus = 0;
        juce::String reason;
        juce::var details;
        juce::String message;
    };

    explicit GmailClient(const juce::File& indexFile = {});
    ~GmailClient();

    void connect(
        StateCallback callback);

    void disconnect();

    State getState() const;

    juce::String getStatusText() const;

    void fetchRecentAudioAttachments(
        int maxMessages,
        LibraryCallback callback);
    void restoreLibrary(LibraryCallback callback);
    bool isLibrarySyncActive() const { return librarySyncActive; }

    void downloadAudioAttachment(
        const juce::String& messageId,
        const juce::String& attachmentId,
        const juce::String& filename,
        const juce::File& destinationDirectory,
        DownloadCallback callback,
        Priority priority = Priority::preview,
        const juce::File& localFile = {});

    void discardQueuedNavigation(const juce::String& keepKey = {});
    void saveAttachment(const juce::File& file, const juce::File& directory,
                        DownloadCallback callback);
    void shutdown();
    uint64_t getSessionGeneration() const { return lifetime->generation.load(); }
    const RequestError& getLastRequestError() const { return lastRequestError; }

    static juce::File getAttachmentCacheFile(
        const juce::String& messageId, const juce::String& attachmentId,
        const juce::String& filename, const juce::File& directory);

private:
    friend class GmailAuthThread;
#if defined(LOOPBRIDGE_GMAIL_TESTS)
    friend int runGmailClientTests();
#endif

    struct Lifetime
    {
        std::atomic<bool> alive { true };
        std::atomic<uint64_t> generation { 0 };
    };
    class Worker;
    struct DiscoveryIndex
    {
        struct Message
        {
            juce::String id, sender, subject;
            std::vector<AudioAttachment> attachments;
        };
        juce::String account;
        std::vector<Message> messages;
        bool load(const juce::File& file);
        bool save(const juce::File& file) const;
        bool knows(const juce::String& id) const;
        std::vector<AudioAttachment> attachments() const;
    };
    struct PendingAttachment
    {
        std::vector<DownloadCallback> callbacks;
    };
    std::shared_ptr<Lifetime> lifetime = std::make_shared<Lifetime>();
    std::unique_ptr<Worker> worker;
    // Message-thread-only: workers capture immutable request data.
    std::map<juce::String, std::shared_ptr<PendingAttachment>> pendingAttachments;
    RequestError lastRequestError; // Message-thread-only structured diagnostics.
    bool librarySyncActive = false;

    struct OAuthCredentials
    {
        juce::String clientId;
        juce::String clientSecret;
    };

    struct TokenData
    {
        juce::String accessToken;
        juce::String refreshToken;
        int expiresIn = 0;
    };

    bool loadCredentials(
        OAuthCredentials& credentials);

    juce::String createCodeVerifier() const;

    juce::String createCodeChallenge(
        const juce::String& verifier) const;

    juce::String createStateToken() const;

    void beginAuthorization();

    void handleAuthorizationCode(
        const juce::String& code);

    static bool exchangeCodeForTokens(
        const juce::String& code,
        TokenData& tokens, const OAuthCredentials& credentials,
        const juce::String& codeVerifier, const juce::String& redirectUri,
        Worker& worker);

    static bool performAuthorizedGet(
        const juce::String& url,
        juce::var& jsonResult,
        juce::String& errorMessage, const juce::String& accessToken, Worker& worker);

    static LibraryUpdate fetchRecentAudioAttachmentsSync(int maxMessages,
        const juce::String& accessToken, Worker& worker);
    static void downloadAudioAttachmentSync(const juce::String& messageId,
        const juce::String& attachmentId, const juce::String& filename,
        const juce::File& destinationDirectory, DownloadCallback callback,
        const juce::String& accessToken, Worker& worker);

    static juce::String getHeaderValue(
        const juce::var& headers,
        const juce::String& name);

    static void collectAudioAttachments(
        const juce::var& part,
        const juce::String& messageId,
        const juce::String& sender,
        const juce::String& subject,
        std::vector<AudioAttachment>& output);

    void setState(
        State newState,
        const juce::String& message);

    static juce::String base64UrlEncode(
        const void* data,
        size_t size);

    State state =
        State::disconnected;

    juce::String statusText =
        "Not connected";

    StateCallback stateCallback;

    OAuthCredentials credentials;

    juce::String accessToken;
    // Until discovery verifies /me/profile, uncached attachment acquisition has
    // no access token. Cached files remain available through the existing path.
    juce::String unverifiedAccessToken;
    juce::String refreshToken;

    juce::String codeVerifier;
    juce::String stateToken;
    juce::String redirectUri;

    int callbackPort = 0;

    std::unique_ptr<juce::Thread>
        authThread;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
        GmailClient)
};
