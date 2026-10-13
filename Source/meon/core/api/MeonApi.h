// MEON core - 서버 REST API (API.md 3~9, 13장). 요청을 JSON 으로 만들어 RestTransport 로 보내고, 응답을 구조체로 바꾼다.
// 공통 규칙도 여기서 한다: Authorization 헤더, X-Meon-Build, 401 token_expired → refresh → 한 번 재시도.
// 모든 함수는 메시지 스레드에서 부르고, 결과 콜백도 메시지 스레드로 온다.
#pragma once

#include "ApiTypes.h"
#include "ApiError.h"
#include "Transport.h"

namespace meon::core
{

template <typename T>
using Callback = std::function<void (Result<T>)>;

class MeonApi
{
public:
    MeonApi (RestTransport&, int buildNumber);
    ~MeonApi();

    //==============================================================================
    // 토큰
    void setTokens (const Tokens&);
    const Tokens& getTokens() const        { return tokens; }
    bool hasSession() const                 { return tokens.refreshToken.isNotEmpty(); }
    /** 토큰이 바뀔 때마다 (로그인, 가입, 회전). 빈 값이면 세션이 끝난 것 */
    std::function<void (const Tokens&)> onTokensChanged;
    /** refresh 가 실패해서 다시 로그인해야 할 때 */
    std::function<void()> onSessionLost;
    /** 426 client_outdated 를 받았을 때 (화면이 강제 업데이트 동작으로) */
    std::function<void()> onClientOutdated;

    //==============================================================================
    // 13
    void getConfig (Callback<Config>);

    // 3 계정
    void usernameAvailable (const juce::String& username, Callback<bool>);
    void signup (const juce::String& email, const juce::String& username, const juce::String& password, bool agreedTerms, Callback<AuthResult>);
    void login (const juce::String& email, const juce::String& password, Callback<AuthResult>);
    void refresh (Callback<Tokens>);
    void logout (Callback<Done>);
    void verifyEmail (const juce::String& code, Callback<Me>);
    void resendVerification (Callback<Done>);

    // 4 내 계정
    void getMe (Callback<Me>);
    void patchMe (const MePatch&, Callback<Me>);
    void changePassword (const juce::String& current, const juce::String& next, Callback<Done>);
    /** 인증 전 계정은 password 를 비워 보낸다 (API 4.4) */
    void requestEmailChange (const juce::String& newEmail, const juce::String& password, Callback<Done>);
    void verifyEmailChange (const juce::String& code, Callback<Me>);
    void deleteAccount (const juce::String& password, Callback<Done>);
    void getUser (const juce::String& userId, Callback<UserProfile>);

    // 5 방
    void listPublicRooms (Callback<std::vector<Room>>);
    void createRoom (const CreateRoom&, Callback<RoomDetail>);
    void getRoom (const juce::String& roomId, Callback<RoomDetail>);   // 멤버가 아니면 connection 이 비어 있다
    void joinRoom (const juce::String& roomId, Callback<JoinResult>);
    void joinByCode (const juce::String& code, Callback<JoinResult>);
    void leaveRoom (const juce::String& roomId, Callback<Done>);
    void reportRtt (int aooRttMs, Callback<Done>);
    void sendLatencyTelemetry (const juce::String& roomId, int estimatedMs, int myRttMs, int hostRttMs, int actualP2pRttMs, Callback<Done>);

    // 5.7 승인 요청 (방장)
    void listJoinRequests (const juce::String& roomId, Callback<std::vector<JoinRequest>>);
    void acceptJoinRequest (const juce::String& roomId, const juce::String& requestId, Callback<RoomDetail>);
    void declineJoinRequest (const juce::String& roomId, const juce::String& requestId, Callback<Done>);

    // 6 방장
    void patchRoom (const juce::String& roomId, const RoomPatch&, Callback<RoomDetail>);
    void transferHost (const juce::String& roomId, const juce::String& userId, Callback<RoomDetail>);
    void kick (const juce::String& roomId, const juce::String& userId, Callback<Done>);
    void closeRoom (const juce::String& roomId, Callback<Done>);

    // 7 친구·초대
    void listFriends (Callback<std::vector<Friend>>);
    /** result: sent | not_found | already_friends | already_requested | accepted | self */
    void sendFriendRequest (const juce::String& username, Callback<juce::String>);
    void listFriendRequests (Callback<std::vector<FriendRequest>>);
    void acceptFriendRequest (const juce::String& requestId, Callback<Done>);
    void declineFriendRequest (const juce::String& requestId, Callback<Done>);
    void removeFriend (const juce::String& userId, Callback<Done>);
    void sendInvite (const juce::String& userId, Callback<InviteSent>);
    void cancelInvite (const juce::String& inviteId, Callback<Done>);
    void acceptInvite (const juce::String& inviteId, Callback<JoinResult>);
    void dismissInvite (const juce::String& inviteId, Callback<Done>);

    // 8 차단
    void listBlocks (Callback<std::vector<Block>>);
    void block (const juce::String& userId, Callback<Done>);
    void unblock (const juce::String& userId, Callback<Done>);

    // 9 알림
    void notificationState (Callback<bool>);
    void markNotificationsRead (Callback<Done>);

private:
    RestTransport& transport;
    const int build;
    Tokens tokens;
    bool refreshing = false;
    std::vector<std::function<void (bool)>> waitingForRefresh;
    std::shared_ptr<bool> alive { std::make_shared<bool> (true) };

    using Raw = std::function<void (const HttpResponse&)>;
    void request (const juce::String& method, const juce::String& path, const juce::var& body, bool auth, Raw done);
    void sendNow (const juce::String& method, const juce::String& path, const juce::var& body, bool auth, bool mayRetry, Raw done);
    void doRefresh (std::function<void (bool ok)> done);

    template <typename T>
    void call (const juce::String& method, const juce::String& path, const juce::var& body, bool auth, Callback<T> cb);
    void callDone (const juce::String& method, const juce::String& path, const juce::var& body, bool auth, Callback<Done> cb);
    void callJoin (const juce::String& path, const juce::var& body, Callback<JoinResult> cb);
    void storeAuth (const HttpResponse&, Callback<AuthResult>);

    JUCE_DECLARE_NON_COPYABLE (MeonApi)
};

/** 응답 → 오류 (2xx 가 아니거나 네트워크 오류) */
ApiError errorFromResponse (const HttpResponse&);

} // namespace meon::core
