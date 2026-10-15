#include "AppStore.h"

namespace meon::core
{

using juce::String;

const Room* AppState::findPublicRoom (const String& id) const
{
    for (auto& r : publicRooms)
        if (r.id == id) return &r;
    return nullptr;
}

const Friend* AppState::findFriend (const String& userId) const
{
    for (auto& f : friends)
        if (f.user.id == userId) return &f;
    return nullptr;
}

//==============================================================================
void AppStore::setMe (const Me& m)                { s.me = m; changed (StateKey::me); }
void AppStore::setConfig (const Config& c)        { s.config = c; s.configLoaded = true; changed (StateKey::config); }

void AppStore::setPublicRooms (std::vector<Room> list)
{
    s.publicRooms = std::move (list);
    s.publicRoomsLoaded = true;
    changed (StateKey::publicRooms);
}

void AppStore::upsertPublicRoom (const Room& r)
{
    for (auto& x : s.publicRooms)
        if (x.id == r.id) { x = r; changed (StateKey::publicRooms); return; }
    s.publicRooms.insert (s.publicRooms.begin(), r);   // 새 방은 앞 (생성 시각 내림차순)
    changed (StateKey::publicRooms);
}

void AppStore::setFriends (std::vector<Friend> list)  { s.friends = std::move (list); changed (StateKey::friends); }

void AppStore::setFriendRequests (std::vector<FriendRequest> list)
{
    s.friendRequests = std::move (list);
    changed (StateKey::friendRequests);
}

void AppStore::removeFriendRequest (const String& id)
{
    auto& v = s.friendRequests;
    v.erase (std::remove_if (v.begin(), v.end(), [&] (const FriendRequest& f) { return f.id == id; }), v.end());
    changed (StateKey::friendRequests);
}

void AppStore::setHasUnread (bool b)
{
    if (s.hasUnreadNotifications == b) return;
    s.hasUnreadNotifications = b;
    changed (StateKey::notifications);
}

void AppStore::removeInvite (const String& id)
{
    auto& v = s.invites;
    const auto before = v.size();
    v.erase (std::remove_if (v.begin(), v.end(), [&] (const Invite& i) { return i.id == id; }), v.end());
    if (v.size() != before) changed (StateKey::invites);
}

void AppStore::setCurrentRoom (std::optional<RoomDetail> r)
{
    if (! r) { leaveRoomState(); return; }
    const bool entering = ! s.currentRoom || s.currentRoom->id != r->id;
    s.currentRoom = std::move (r);
    if (entering)
    {
        s.joinRequests.clear();
        // 어느 방에든 들어가면 받아 둔 초대는 모두 소멸 (API 7.11). 서버의 invite.expired 를 기다리지 않고 바로 지운다
        if (! s.invites.empty()) { s.invites.clear(); changed (StateKey::invites); }
        changed (StateKey::joinRequests);
    }
    changed (StateKey::currentRoom);
}

void AppStore::leaveRoomState()
{
    if (! s.currentRoom && s.joinRequests.empty()) return;
    s.currentRoom.reset();
    s.joinRequests.clear();
    changed (StateKey::joinRequests);
    changed (StateKey::currentRoom);
}

void AppStore::setJoinRequests (std::vector<JoinRequest> list)
{
    s.joinRequests = std::move (list);
    changed (StateKey::joinRequests);
}

void AppStore::removeJoinRequest (const String& id)
{
    auto& v = s.joinRequests;
    const auto before = v.size();
    v.erase (std::remove_if (v.begin(), v.end(), [&] (const JoinRequest& j) { return j.id == id; }), v.end());
    if (v.size() != before) changed (StateKey::joinRequests);
}

void AppStore::setMyRtt (int ms)
{
    if (s.myRttMs == OptInt (ms)) return;
    s.myRttMs = ms;
    changed (StateKey::myRtt);
}

void AppStore::setMyJoinRequestUntil (const String& roomId, const String& until)
{
    for (auto& r : s.publicRooms)
        if (r.id == roomId) { r.myJoinRequestUntil = until; changed (StateKey::publicRooms); return; }
}

void AppStore::clear()
{
    s = AppState();
    for (auto k : { StateKey::me, StateKey::config, StateKey::publicRooms, StateKey::friends, StateKey::friendRequests,
                    StateKey::notifications, StateKey::invites, StateKey::currentRoom, StateKey::joinRequests, StateKey::link,
                    StateKey::myRtt })
        changed (k);
}

void AppStore::upsertFriend (const Friend& f)
{
    for (auto& x : s.friends)
        if (x.user.id == f.user.id) { x = f; changed (StateKey::friends); return; }
    s.friends.push_back (f);
    changed (StateKey::friends);
}

//==============================================================================
void AppStore::linkChanged (Realtime::Link l)
{
    if (s.link == l) return;
    s.link = l;
    changed (StateKey::link);
    // 다른 곳에서 같은 계정으로 접속했다 (4000): 방은 그대로 둔다 (leave 를 부르지 않는다. 서버에서도 방에 그대로다).
    // 합주(AOO) 연결만 JamSession 이 link 를 보고 끊는다 (aooserver 가 같은 이름의 두 번째 로그인을 거부).
    // 여기서 다시 연결하면 hello 의 current_room 으로 덮어쓰고, 같은 방이면 같은 Connection 으로 다시 붙는다
}

void AppStore::hello (const Hello& h)
{
    // 전체 상태를 통째로 덮어쓴다 (10.4). 끊긴 사이 놓친 추방·방 닫힘은 current_room 이 비어 있는 것으로 반영된다.
    // 방에 있었는데 다른 방이 오면: 끊긴 사이 같은 계정의 다른 기기가 들어간 방이다 (JamSession 이 송출 정지로 들어간다)
    const auto before = s.currentRoom ? s.currentRoom->id : String();
    // 지난번에 정상 종료하며 나간 방이 아직 남아 있으면(나가기가 서버에 닿지 않음) 그 방에는 들어가지 않는다
    String droppedRoom;
    if (roomLeftOnQuit.isNotEmpty())
    {
        if (h.currentRoom && h.currentRoom->id == roomLeftOnQuit) droppedRoom = roomLeftOnQuit;
        roomLeftOnQuit = {};
    }
    s.me = h.me;
    s.config = h.config;
    s.configLoaded = true;
    s.currentRoom = droppedRoom.isEmpty() ? h.currentRoom : std::nullopt;
    s.joinRequests = droppedRoom.isEmpty() ? h.joinRequests : std::vector<JoinRequest>();
    s.friends = h.friends;
    s.hasUnreadNotifications = h.hasUnreadNotifications;
    s.invites = h.pendingInvites;
    for (auto k : { StateKey::me, StateKey::config, StateKey::currentRoom, StateKey::joinRequests, StateKey::friends,
                    StateKey::notifications, StateKey::invites })
        changed (k);
    if (before.isNotEmpty() && s.currentRoom && s.currentRoom->id != before)
        notify ({ Notice::Kind::movedToOtherRoom, s.currentRoom->id, s.currentRoom->title, {} });
    if (droppedRoom.isNotEmpty() && onRoomLeftOnQuit) onRoomLeftOnQuit (droppedRoom);
}

void AppStore::event (const RealtimeEvent& e)
{
    using K = RealtimeEvent::Kind;
    switch (e.kind)
    {
        case K::roomsAdded:
        case K::roomsUpdated:
            upsertPublicRoom (*e.room);
            break;

        case K::roomsRemoved:
        {
            auto& v = s.publicRooms;
            v.erase (std::remove_if (v.begin(), v.end(), [&] (const Room& r) { return r.id == e.roomId; }), v.end());
            changed (StateKey::publicRooms);
            break;
        }

        case K::roomUpdated:
            if (s.currentRoom && s.currentRoom->id == e.roomDetail->id)
            {
                s.currentRoom = *e.roomDetail;
                changed (StateKey::currentRoom);
            }
            break;

        case K::roomKicked:
        case K::roomClosed:
        {
            if (! s.currentRoom || s.currentRoom->id != e.roomId) break;
            Notice n { e.kind == K::roomKicked ? Notice::Kind::kicked : Notice::Kind::roomClosed, e.roomId, s.currentRoom->title, {} };
            leaveRoomState();
            notify (n);
            break;
        }

        case K::joinRequestReceived:
            for (auto& j : s.joinRequests)
                if (j.id == e.joinRequest->id) return;
            s.joinRequests.push_back (*e.joinRequest);
            changed (StateKey::joinRequests);
            break;

        case K::joinRequestRemoved:
            removeJoinRequest (e.requestId);
            break;

        case K::joinRequestAccepted:
        {
            setCurrentRoom (*e.roomDetail);
            notify ({ Notice::Kind::joinAccepted, e.roomDetail->id, e.roomDetail->title, {} });
            break;
        }

        case K::friendUpdated:
        case K::friendAdded:
            upsertFriend (*e.friendInfo);
            break;

        case K::friendRemoved:
        {
            auto& v = s.friends;
            v.erase (std::remove_if (v.begin(), v.end(), [&] (const Friend& f) { return f.user.id == e.userId; }), v.end());
            changed (StateKey::friends);
            break;
        }

        case K::friendRequestReceived:
            s.friendRequests.insert (s.friendRequests.begin(), *e.friendRequest);
            changed (StateKey::friendRequests);
            setHasUnread (true);
            break;

        case K::inviteReceived:
            if (s.currentRoom) break;   // 방 안에서는 초대가 오지 않는다 (서버가 버린다). 혹시 오면 무시
            s.invites.push_back (*e.invite);
            changed (StateKey::invites);
            notify ({ Notice::Kind::inviteReceived, e.invite->room.id, e.invite->room.title, e.invite });
            break;

        case K::inviteExpired:
            removeInvite (e.inviteId);
            break;

        case K::configUpdated:
            setConfig (*e.config);
            break;

        case K::unknown:
            break;
    }
}

} // namespace meon::core
