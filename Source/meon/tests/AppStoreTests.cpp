// AppStore: 이벤트를 상태에 반영하는 규칙
#include "../core/state/AppStore.h"
#include "../core/api/ApiJson.h"

namespace meon::core
{

class AppStoreTests : public juce::UnitTest, private AppStore::Listener
{
public:
    AppStoreTests() : juce::UnitTest ("AppStore", "State") {}

    std::vector<StateKey> keys;
    void stateChanged (StateKey k) override { keys.push_back (k); }

    static RealtimeEvent ev (const char* type, const juce::var& data)
    {
        RealtimeEvent e;
        static int seq = 0;
        RealtimeEvent::decode (RealtimeEvent::encode (type, ++seq, data), e);
        return e;
    }

    static Room room (const char* id)
    {
        Room r;
        r.id = id;
        r.title = id;
        r.hostId = "u_h";
        r.memberCount = 1;
        r.createdAt = "2026-10-13T07:33:00Z";
        return r;
    }

    void runTest() override
    {
        AppStore store;
        store.addListener (this);

        beginTest ("rooms.* events");
        store.setPublicRooms ({ room ("a"), room ("b") });
        store.event (ev ("rooms.added", json::encode (room ("c"))));
        expectEquals ((int) store.state().publicRooms.size(), 3);
        expectEquals (store.state().publicRooms.front().id, juce::String ("c"));
        auto changedB = room ("b");
        changedB.title = "B2";
        store.event (ev ("rooms.updated", json::encode (changedB)));
        expectEquals (store.state().findPublicRoom ("b")->title, juce::String ("B2"));
        store.event (ev ("rooms.removed", json::obj ({ { "room_id", "a" } })));
        expect (store.state().findPublicRoom ("a") == nullptr);

        beginTest ("rooms with no members are not shown (closing; GET /rooms can read the room before and its members after the last leave)");
        {
            auto empty = room ("z");
            empty.memberCount = 0;
            store.setPublicRooms ({ room ("a"), empty, room ("b") });
            expect (store.state().findPublicRoom ("z") == nullptr && store.state().publicRooms.size() == 2, "목록 응답의 0명 방");
            store.event (ev ("rooms.added", json::encode (empty)));
            expect (store.state().findPublicRoom ("z") == nullptr, "rooms.added 0명");
            auto emptied = room ("a");
            emptied.memberCount = 0;
            store.event (ev ("rooms.updated", json::encode (emptied)));
            expect (store.state().findPublicRoom ("a") == nullptr, "rooms.updated 로 0명이 되면 뺀다");
            expect (store.state().findPublicRoom ("b") != nullptr);
        }

        beginTest ("friend request → unread dot");
        keys.clear();
        FriendRequest fr { "fr_1", { "u_2", "junho", juce::String ("준호"), juce::String ("guitar") }, "2026-10-13T07:33:00Z" };
        store.event (ev ("friend_request.received", json::encode (fr)));
        expect (store.state().hasUnreadNotifications);
        expectEquals ((int) store.state().friendRequests.size(), 1);
        expect (std::find (keys.begin(), keys.end(), StateKey::notifications) != keys.end());

        beginTest ("room.updated only for my room; kicked clears it");
        RoomDetail mine;
        static_cast<Room&> (mine) = room ("m");
        mine.connection.members = { "u_me" };
        store.setCurrentRoom (mine);
        auto other = mine;
        other.id = "x";
        other.title = "nope";
        store.event (ev ("room.updated", json::encode (other)));
        expectEquals (store.state().currentRoom->title, juce::String ("m"));
        auto upd = mine;
        upd.memberCount = 2;
        store.event (ev ("room.updated", json::encode (upd)));
        expectEquals (store.state().currentRoom->memberCount, 2);
        store.event (ev ("room.kicked", json::obj ({ { "room_id", "other" } })));
        expect (store.state().inRoom());
        store.event (ev ("room.kicked", json::obj ({ { "room_id", "m" } })));
        expect (! store.state().inRoom());

        beginTest ("invites: ignored in a room, cleared on entry, removed on expiry");
        Invite inv { "inv_1", fr.from, { "r_1", "t" }, "2026-10-13T07:33:00Z" };
        store.event (ev ("invite.received", json::encode (inv)));
        expectEquals ((int) store.state().invites.size(), 1);
        store.event (ev ("invite.expired", json::obj ({ { "invite_id", "inv_1" } })));
        expect (store.state().invites.empty());
        store.event (ev ("invite.received", json::encode (inv)));
        store.setCurrentRoom (mine);
        expect (store.state().invites.empty());
        store.event (ev ("invite.received", json::encode (inv)));
        expect (store.state().invites.empty());

        beginTest ("hello overwrites everything");
        Hello h;
        h.me.id = "u_me";
        h.me.username = "me";
        h.config.latestBuild = 7;
        store.hello (h);
        expect (! store.state().inRoom());
        expect (! store.state().hasUnreadNotifications);
        expectEquals (store.state().config.latestBuild, 7);
        expect (store.state().friends.empty());

        store.removeListener (this);
    }
};

static AppStoreTests appStoreTests;

} // namespace meon::core
