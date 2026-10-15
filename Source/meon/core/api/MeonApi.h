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

/** 같은 로그인(refresh 토큰 저장소)을 쓰는 다른 프로세스(독립 앱·플러그인)와 토큰 갱신을 나눠 쓰는 방법.
    refresh 토큰은 한 번 쓰면 바뀌므로, 두 프로세스가 각자 들고 있는 옛 토큰으로 갱신하면 서버가 재사용(도용)으로 보고
    모든 기기를 로그아웃시킬 수 있다. 그래서 갱신할 때는 잠그고, 저장소의 지금 토큰을 다시 읽어 쓴다. */
struct RefreshCoordinator
{
    virtual ~RefreshCoordinator() = default;
    /** 다른 프로세스가 갱신 중이 아니면 잠그고 true (기다리지 않는다) */
    virtual bool tryLock() = 0;
    virtual void unlock() = 0;
    /** 저장소의 지금 refresh 토큰 (다른 프로세스가 바꿨을 수 있다). 못 읽으면 빈 문자열 */
    virtual juce::String storedRefreshToken() = 0;
    /** 잠그지 못했을 때 ms 뒤 다시 시도 (메시지 스레드) */
    virtual void retryLater (int ms, std::function<void()>) = 0;
};

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
    int buildNumber() const                 { return build; }
    /** 다른 프로세스와 갱신을 나눠 쓴다 (없으면 이 프로세스 안에서만 하나씩). 소유하지 않는다 */
    void setRefreshCoordinator (RefreshCoordinator* c) { coordinator = c; }
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
    /** 토큰 갱신. 이미 갱신 중이면 새로 보내지 않고 그 결과를 같이 받는다 (refresh 토큰은 한 번 쓰면 바뀌므로
        두 번 보내면 늦게 간 쪽이 이미 지워진 토큰을 보내 거절된다). 실패(네트워크 외)면 토큰을 비운다.
        why 는 진단 로그용 (누가 갱신을 불렀나) */
    void refresh (Callback<Tokens>, const juce::String& why = {});
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
    /** 본문 없이 로그인된 세션만으로 (API 4.5, v0.5) */
    void deleteAccount (Callback<Done>);
    void getUser (const juce::String& userId, Callback<UserProfile>);

    // 5 방
    void listPublicRooms (Callback<std::vector<Room>>);
    void createRoom (const CreateRoom&, Callback<RoomDetail>);
    void getRoom (const juce::String& roomId, Callback<RoomDetail>);   // 멤버가 아니면 connection 이 비어 있다
    void joinRoom (const juce::String& roomId, Callback<JoinResult>);
    void joinByCode (const juce::String& code, Callback<JoinResult>);
    void leaveRoom (const juce::String& roomId, Callback<Done>);
    /** 앱·플러그인을 정상 종료할 때: 나가기를 바로 보내고 응답을 기다린다 (최대 timeoutMs). 토큰을 갱신하지 않는다
        (access 토큰이 만료됐으면 401 로 실패). 서버가 처리했으면 true (멤버가 아닌 방도 204, API 5.6) */
    bool leaveRoomBlocking (const juce::String& roomId, int timeoutMs);
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
    RefreshCoordinator* coordinator = nullptr;
    bool coordinatorLocked = false;
    bool sessionLostOnRefreshFailure = false;       // 요청 도중 401 로 시작한 갱신이면 실패 때 onSessionLost
    std::vector<Callback<Tokens>> refreshWaiters;   // 진행 중인 갱신의 결과를 기다리는 쪽 (요청 재시도 포함)
    std::shared_ptr<bool> alive { std::make_shared<bool> (true) };

    using Raw = std::function<void (const HttpResponse&)>;
    void request (const juce::String& method, const juce::String& path, const juce::var& body, bool auth, Raw done);
    void sendNow (const juce::String& method, const juce::String& path, const juce::var& body, bool auth, bool mayRetry, Raw done);
    void doRefresh (std::function<void (bool ok)> done, const juce::String& why);
    void sendRefresh (int attempt);
    void finishRefresh (const HttpResponse&);

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
