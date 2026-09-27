#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <vector>

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

    using AudioListCallback =
        std::function<void(
            const std::vector<AudioAttachment>&,
            const juce::String&)>;

    GmailClient();
    ~GmailClient();

    void connect(
        StateCallback callback);

    void disconnect();

    State getState() const;

    juce::String getStatusText() const;

    void fetchRecentAudioAttachments(
        int maxMessages,
        AudioListCallback callback);

private:
    friend class GmailAuthThread;

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

    bool exchangeCodeForTokens(
        const juce::String& code,
        TokenData& tokens);

    bool performAuthorizedGet(
        const juce::String& url,
        juce::var& jsonResult,
        juce::String& errorMessage) const;

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