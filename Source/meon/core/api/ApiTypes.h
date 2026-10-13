// MEON core - API.md 2장 데이터 모델을 그대로 옮긴 구조체.
// 필드 이름은 camelCase, JSON 은 snake_case (ApiJson 이 바꾼다). ID·시각은 서버가 준 문자열 그대로 둔다.
// JSON 의 null 은 std::optional 로 구분한다 (빈 문자열과 다르다).
#pragma once

#include <JuceHeader.h>
#include <optional>
#include <vector>

namespace meon::core
{

using OptString = std::optional<juce::String>;
using OptInt    = std::optional<int>;

// 2.1 Part: "vocal" | "guitar" | "bass" | "drums" | "keys" | "other" (model/Parts 에 목록)

// 2.2
struct UserPublic
{
    juce::String id, username;
    OptString nickname, part;   // 다른 사람은 항상 값이 있다. 온보딩 중인 내 계정만 null
};

// 2.3
struct MeSettings
{
    juce::String friendRequests = "everyone";   // "everyone" | "none"
    juce::String roomInvites    = "friends";    // "friends" | "none"
};

struct Me
{
    juce::String id, username;
    OptString nickname, part;
    juce::String email;
    bool emailVerified = false;
    OptString pendingEmail;
    MeSettings settings;
    juce::String createdAt;

    UserPublic asPublic() const { return { id, username, nickname, part }; }
};

// 2.4, 2.10 의 room
struct RoomRef
{
    juce::String id, title;
    bool joinable = false;
};

struct UserProfile
{
    UserPublic user;
    juce::String relation = "none";   // none | friend | request_sent | request_received | blocked_by_me
    OptString status;                 // 친구일 때만: offline | online | in_room
    OptString lastSeenAt;
    std::optional<RoomRef> room;
    bool canSendFriendRequest = false;
    bool canInvite = false;
};

// 2.6
struct RoomMember
{
    UserPublic user;
    bool isHost = false;
    bool isFriend = false;
    juce::String joinedAt;
};

// 2.5
struct Room
{
    juce::String id;
    juce::String type = "public";   // public | private
    OptString entryMode;            // 공개 방만: instant | approval. 비공개는 null
    juce::String title, description;
    int capacity = 7;
    int memberCount = 0;
    juce::String hostId;
    bool hostPresent = false;
    OptInt hostRttMs;
    std::vector<RoomMember> members;
    OptString myJoinRequestUntil;
    juce::String createdAt;

    bool isPublic() const   { return type == "public"; }
    bool isApproval() const { return isPublic() && entryMode == juce::String ("approval"); }
};

// 2.8
struct Connection
{
    juce::String aooHost;
    int aooPort = 0;
    juce::String groupName, groupPassword;
    juce::String aooUsername, aooUserPassword;
    juce::StringArray members;
};

// 2.7
struct RoomDetail : Room
{
    OptString inviteCode;
    Connection connection;
};

// 2.9
struct FriendRequest
{
    juce::String id;
    UserPublic from;
    juce::String createdAt;
};

// 2.10
struct Friend
{
    UserPublic user;
    juce::String status = "offline";   // offline | online | in_room
    OptString lastSeenAt;
    std::optional<RoomRef> room;
    bool canInvite = false;
};

// 2.12
struct InviteRoom
{
    juce::String id, title;
};

struct Invite
{
    juce::String id;
    UserPublic from;
    InviteRoom room;
    juce::String createdAt;
};

// 2.13
struct JoinRequest
{
    juce::String id, roomId;
    UserPublic user;
    bool viaInvite = false;
    juce::String createdAt, expiresAt;
};

// 8.2
struct Block
{
    UserPublic user;
    juce::String createdAt;
};

// 13
struct Config
{
    int minBuild = 0;
    int latestBuild = 0;
    double latencyCoefficient = 0.7;
    juce::String discordUrl;
};

// 10.4
struct Hello
{
    Me me;
    std::optional<RoomDetail> currentRoom;
    std::vector<JoinRequest> joinRequests;
    std::vector<Friend> friends;
    bool hasUnreadNotifications = false;
    std::vector<Invite> pendingInvites;
    Config config;
};

//==============================================================================
// 요청·응답 묶음

struct Tokens
{
    juce::String accessToken, refreshToken;
};

struct AuthResult   // 3.3, 3.7
{
    Tokens tokens;
    Me me;
};

struct JoinResult   // 5.5 / 5.7: 200 이면 room, 202 면 requestUntil
{
    std::optional<RoomDetail> room;
    OptString requestUntil;

    bool requested() const { return ! room.has_value(); }
};

struct InviteSent   // 7.7
{
    juce::String id, deliverAt;
};

struct MePatch      // 4.2: 값이 있는 필드만 보낸다
{
    OptString nickname, part, friendRequests, roomInvites;
};

struct CreateRoom   // 5.3
{
    juce::String type = "public";
    OptString entryMode;
    juce::String title, description;
    int capacity = 7;
};

struct RoomPatch    // 6.1: 값이 있는 필드만 보낸다. entryMode 는 null 을 보낼 수 있어 따로 표시
{
    OptString title, description, type;
    OptInt capacity;
    bool setEntryMode = false;
    OptString entryMode;
};

} // namespace meon::core
