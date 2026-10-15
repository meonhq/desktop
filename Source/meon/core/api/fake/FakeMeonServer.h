// MEON core - 가짜 서버. 진짜 서버가 생기기 전까지 RestTransport·RealtimeTransport 를 메모리 안에서 구현한다.
// 요청은 진짜와 같은 경로·JSON 으로 받고, 응답·이벤트도 API.md 모양의 JSON 으로 돌려준다 (클라이언트는 같은 디코더를 쓴다).
// 서버 규칙(정원, 승인 요청, 3초 초대, 차단 필터, 숨은 친구 요청 …)은 API.md 대로 작게 구현한다.
// 다른 사용자·공개 방·친구는 fixtures/world.json 과 각본(FakeWorld.cpp)이 만든다.
//
// 가짜 기간의 실제 합주 (REMAKE_PLAN 1.5): Connection 은 지금 AOO 서버를 쓰고, 비공개 방은 입장 코드를 그룹 이름으로,
// 그룹 비밀번호는 코드에서 계산한다. 그래서 다른 컴퓨터의 가짜 서버와도 같은 AOO 그룹에서 만난다.
#pragma once

#include "../Transport.h"
#include "../ApiTypes.h"
#include "../ApiError.h"
#include "../../util/Scheduler.h"
#include <map>
#include <set>

namespace meon::core
{

class FakeMeonServer : public RestTransport,
                       public RealtimeTransport
{
public:
    struct Options
    {
        int latencyMs = 120;                  // 응답·이벤트 지연
        // SonoBus 공개 AOO 서버: 서버 없는 PR 빌드로 두 컴퓨터 합주를 시험한다 (빌드 0 전용, 실제 번호 빌드는 가짜 서버를 못 고른다).
        // 운영 서버 + 실제 번호 빌드로 두 대 합주를 확인한 뒤 지운다 (REMAKE_PLAN 1.5). 로컬 aooserver 는 --fake-aoo=host:port
        juce::String aooHost = "aoo.sonobus.net";
        int aooPort = 10998;
        bool scripts = true;                  // 다른 사용자 각본
        double scriptSpeed = 1.0;             // 2.0 이면 두 배 빠르게
        bool fakeHostsAccept = true;          // 가짜 방장이 내 승인 요청을 수락
        juce::uint32 seed = 0;                // 0 이면 시각으로
    };

    FakeMeonServer (Scheduler&, Options);
    ~FakeMeonServer() override;

    /** fixtures/world.json 내용 */
    void loadWorld (const juce::var& world);
    /** 이 설치에서 만든 계정(가짜 계정 ID·AOO 비밀번호·refresh 토큰)을 되살린다 */
    void loadState (const juce::var& saved);
    juce::var saveState() const;
    /** 계정 정보가 바뀔 때마다 (저장할 때) */
    std::function<void (const juce::var&)> onStateChanged;

    //==============================================================================
    // RestTransport
    void send (const HttpRequest&, std::function<void (HttpResponse)> done) override;
    /** 지연 없이 바로 처리 (테스트용). 이벤트는 그대로 대기열로 간다 */
    HttpResponse handle (const HttpRequest&);

    // RealtimeTransport
    void open (Sink*) override;
    void send (const juce::var& message) override;
    void close() override;

    //==============================================================================
    // 개발용 (개발 빌드 화면·명령줄·테스트)
    /** 마지막으로 "메일로 보낸" 6자리 코드 (가입 인증·이메일 변경). 개발 빌드는 화면에 보여 준다 */
    juce::String getLastMailedCode() const { return lastMailedCode; }
    void devDropLink (int seconds);             // 실시간 연결 끊김 흉내
    void devCloseLink (int code);               // 서버가 닫는 코드로 닫음 흉내 (4000 다른 곳에서 접속, 4001 세션 폐기, 4002 느림)
    void devKickMe();                           // 내가 있는 방의 방장이 나를 내보냄 (가짜 방장일 때)
    void devCloseMyRoom();                      // 내가 있는 방의 가짜 방장이 방을 닫음
    void devSetConfig (int minBuild, int latestBuild);
    void devFriendRequest();                    // 가짜 사용자가 나에게 친구 요청
    void devInviteMe();                         // 가짜 친구가 나를 초대
    void devJoinRequest();                      // 내가 방장인 승인 방에 가짜 사용자가 요청
    void setScriptsEnabled (bool);

    /** 가짜 기간: 실제 AOO 상대가 내 방(그룹)에 들어오고 나갈 때 (FakeAooDirectory 가 부른다, R4) */
    void remotePeerJoined (const juce::String& userId, const juce::String& nickname, const juce::String& part, bool isHost);
    void remotePeerLeft (const juce::String& userId);

    /** 방 코드 → AOO 그룹 비밀번호 (두 컴퓨터가 같은 값을 계산) */
    static juce::String groupPasswordFor (const juce::String& groupName);

    //==============================================================================
    // 테스트가 상태를 들여다볼 때
    juce::String meId() const { return wsUser; }
    /** 테스트용: 실시간 이벤트를 받는 사람(실시간 연결이 없는 가짜 사용자 포함)과 함께 모두 알려 준다 */
    std::function<void (const juce::String& toUser, const juce::String& type, const juce::var& data)> onEmitForTest;
    bool isMember (const juce::String& roomId, const juce::String& userId) const;
    int pendingInviteCountFor (const juce::String& userId) const;

    // 규칙 상수 (API.md)
    static constexpr int inviteDelayMs = 3000;
    static constexpr int joinRequestLifeMs = 60000;
    static constexpr int requestUntilMs = 5 * 60 * 1000;
    static constexpr int accessLifeMs = 15 * 60 * 1000;
    // refresh 재사용 유예: 회전한 뒤 새 토큰이 한 번도 쓰이지 않았으면 옛 토큰을 이 시간까지 받아 준다.
    // 진짜 서버 반영 대기 (지금 진짜 서버 API 1.3 은 10초 유예. 같은 규칙으로 바꾸는 일은 서버 쪽에서 따로 한다)
    static constexpr int refreshReuseGraceMs = 10 * 60 * 1000;
    static constexpr int codeLifeMs = 10 * 60 * 1000;
    static constexpr int resendGapMs = 30000;
    static constexpr int closedMemoryMs = 24 * 60 * 60 * 1000;

private:
    //==============================================================================
    struct User
    {
        juce::String id, username, email;
        juce::String salt, passwordHash;
        OptString nickname, part, pendingEmail;
        bool verified = false;
        MeSettings settings;
        juce::int64 createdAt = 0;
        bool local = false;          // 이 설치에서 만든 계정 (저장 대상)
        bool online = false;         // 가짜 사용자: 각본이 정한다. 내 계정: 실시간 연결이 있을 때
        juce::int64 lastSeenAt = 0;
        bool deleted = false;
        OptInt rttMs;
        juce::String aooPassword;
        // 메일 코드
        juce::String verifyCode, emailCode;
        juce::int64 verifyCodeAt = 0, emailCodeAt = 0, lastMailAt = 0;
        int verifyFails = 0, emailFails = 0;
        juce::StringArray refreshTokens;
        bool worldAttached = false;  // 가짜 친구·요청·차단을 붙였는지
    };

    struct MemberRec { juce::String userId; juce::int64 joinedAt; };

    struct RoomRec
    {
        juce::String id, type = "public";
        OptString entryMode;
        juce::String title, description;
        int capacity = 7;
        juce::String hostId;
        std::vector<MemberRec> members;
        juce::String inviteCode;
        juce::String groupName;
        std::set<juce::String> kicked;
        juce::int64 createdAt = 0;
        bool extra = false;          // 각본이 만들었다 지우는 방
    };

    struct FriendReqRec { juce::String id, from, to; juce::int64 at; bool hidden = false, read = false; };
    struct JoinReqRec { juce::String id, roomId, userId; bool viaInvite = false; juce::int64 at = 0, expiresAt = 0; };
    struct InviteRec { juce::String id, from, to, roomId; juce::int64 at = 0, deliverAt = 0; bool delivered = false; };
    struct Session { juce::String userId; juce::int64 expiresAt; };
    struct RequestUntil { juce::String roomId; juce::int64 until; };

    using Body = juce::var;
    struct Out { int status; Body body; };

    Scheduler& scheduler;
    Options options;
    juce::Random random;
    std::shared_ptr<bool> alive { std::make_shared<bool> (true) };

    Config config;
    std::map<juce::String, User> users;
    std::map<juce::String, RoomRec> rooms;
    std::map<juce::String, juce::int64> closedRooms;   // 방 ID 또는 코드 → 닫힌 시각 (24시간 410)
    std::vector<FriendReqRec> friendRequests;
    std::set<juce::String> friendships;                 // "a|b" (작은 ID 먼저)
    std::map<juce::String, std::map<juce::String, juce::int64>> blocks;   // 차단한 사람 → 차단당한 사람 → 시각
    std::vector<JoinReqRec> joinRequests;
    std::map<juce::String, RequestUntil> requestUntil;  // 신청자 → 요청 보냄 표시
    std::vector<InviteRec> invites;
    std::map<juce::String, Session> accessTokens;
    std::map<juce::String, juce::String> refreshOwners;  // refresh → 사용자
    // 회전해서 지운 refresh → 그때 준 새 refresh·회전 시각 (재사용 유예와 도용 판단용, 진짜 서버 반영 대기)
    struct Rotation { juce::String userId, issued; juce::int64 at = 0; };
    std::map<juce::String, Rotation> rotatedRefresh;
    std::map<juce::String, juce::String> fixedCodes;     // 시험용 코드 → 오류 키
    juce::String lastMailedCode;

    // 실시간
    Sink* sink = nullptr;
    bool wsOpen = false, subscribed = false;
    juce::String wsUser;           // 인증된 사용자 (= 이 앱의 나)
    int seq = 0;
    juce::int64 dropUntil = 0;
    int connectionGen = 0;

    // 각본 (FakeWorld.cpp)
    juce::var worldData;
    std::vector<juce::var> extraPool;
    juce::StringArray joiners, joinRequesters, inviters;
    std::vector<std::pair<juce::String, juce::var>> worldFriends;
    juce::StringArray worldFriendRequests, worldBlocks;
    int scriptTask = 0;
    void scheduleScript();
    void scriptTick();
    void attachWorld (User& me);
    juce::String randomUserNotInRoom (const juce::StringArray& from);

    //==============================================================================
    juce::int64 now() const { return scheduler.nowMs(); }
    juce::String newId (const char* prefix);
    juce::String newRoomCode();
    static juce::String hashPassword (const juce::String& salt, const juce::String& pw);
    juce::String newMailCode();
    void stateChanged();

    User* findUser (const juce::String& id);
    const User* findUser (const juce::String& id) const;
    User* findByEmail (const juce::String& email);
    User* findByUsername (const juce::String& username);
    RoomRec* findRoom (const juce::String& id);
    RoomRec* roomOf (const juce::String& userId);
    const RoomRec* roomOf (const juce::String& userId) const;
    RoomRec* roomByCode (const juce::String& code);

    bool areFriends (const juce::String& a, const juce::String& b) const;
    bool hasBlocked (const juce::String& by, const juce::String& target) const;
    bool blockedEitherWay (const juce::String& a, const juce::String& b) const;
    bool hostPresent (const RoomRec&) const;
    bool hasRoomFor (const RoomRec&, const juce::String& userId) const;
    bool isOnline (const juce::String& userId) const;

    // 보는 사람 기준 JSON
    UserPublic publicOf (const User&) const;
    UserPublic publicOf (const juce::String& id) const;
    Me meOf (const User&) const;
    Room roomView (const RoomRec&, const juce::String& viewer) const;
    RoomDetail detailView (const RoomRec&, const juce::String& viewer) const;
    Friend friendView (const juce::String& friendId, const juce::String& viewer) const;
    std::vector<Friend> friendsOf (const juce::String& viewer) const;
    JoinRequest joinRequestView (const JoinReqRec&) const;
    Invite inviteView (const InviteRec&) const;
    Hello helloFor (const juce::String& userId) const;
    bool hasUnread (const juce::String& userId) const;

    // 이벤트
    void emit (const juce::String& toUser, const juce::String& type, const juce::var& data);
    void emitRooms (const juce::String& type, const RoomRec&);
    void emitRoomRemoved (const juce::String& roomId, const juce::String& hostId);
    void roomChanged (const RoomRec&, bool publicListChanged = true);
    void friendChanged (const juce::String& userId);
    void profileChanged (const juce::String& userId);
    void sendHello();

    // 규칙
    Out joinCommon (const juce::String& actor, RoomRec&, bool viaInviteFrom, const juce::String& inviterId);
    void addMember (RoomRec&, const juce::String& userId);
    // 아래 함수들은 컨테이너에서 지우므로, 지울 원소 안의 문자열을 참조로 받으면 지운 뒤 그 참조가 끊긴다.
    // 그래서 값으로 받는다 (호출하는 쪽이 r->id, it->userId 처럼 넘겨도 안전하게)
    void removeMember (RoomRec&, juce::String userId);
    void deleteRoom (juce::String roomId, bool closedByHost);
    void expireInvitesTo (juce::String userId);
    void removeJoinRequestsOf (juce::String userId);
    void removeJoinRequestsForRoom (juce::String roomId);
    void deliverInvite (const juce::String& inviteId);
    void expireJoinRequest (juce::String requestId);
    Connection connectionFor (const RoomRec&, const juce::String& viewer) const;
    void fakeHostConsider (const juce::String& requestId);
    void fakeUserAcceptFriend (const juce::String& requestId);
    void fakeUserAcceptInvite (const juce::String& inviteId);

    // REST
    Out route (const HttpRequest&);
    Out authorised (const HttpRequest&, const juce::String& method, const juce::StringArray& seg, const juce::StringPairArray& query);
    static Out ok (const juce::var& body) { return { 200, body }; }
    static Out created (const juce::var& body) { return { 201, body }; }
    static Out noContent() { return { 204, {} }; }
    static Out fail (ErrorCode c, const juce::String& msg = {}, const juce::String& field = {});
    /** rotatedFrom: refresh 회전이면 지운 옛 토큰 (새로 준 토큰을 기록한 뒤 상태를 저장한다) */
    Out issueTokens (User&, int status, bool withMe, const juce::String& rotatedFrom = {});
    void revokeAllSessions (const juce::String& userId);

    JUCE_DECLARE_NON_COPYABLE (FakeMeonServer)
};

} // namespace meon::core
