// API.md 2장 예시 JSON 을 읽어 구조체로 → 다시 JSON → 키·타입·null 이 같은지.
// 예시의 /* UserPublic */ 같은 자리는 실제 값으로 채웠다.
#include "../core/api/ApiJson.h"
#include "../core/api/Realtime.h"

namespace meon::core
{

class ApiJsonTests : public juce::UnitTest
{
public:
    ApiJsonTests() : juce::UnitTest ("ApiJson", "Api") {}

    static juce::var parse (const char* text)
    {
        auto v = juce::JSON::parse (juce::String::fromUTF8 (text));
        jassert (! v.isVoid());
        return v;
    }

    /** 키 집합·값 타입·값이 같은지 (숫자는 int/double 구분 없이 값 비교) */
    static bool same (const juce::var& a, const juce::var& b, juce::String& where)
    {
        if (a.isArray() || b.isArray())
        {
            auto* xa = a.getArray();
            auto* xb = b.getArray();
            if (xa == nullptr || xb == nullptr || xa->size() != xb->size()) { where = "array"; return false; }
            for (int i = 0; i < xa->size(); ++i)
                if (! same (xa->getReference (i), xb->getReference (i), where)) { where = "[" + juce::String (i) + "]" + where; return false; }
            return true;
        }
        if (a.isObject() || b.isObject())
        {
            auto* oa = a.getDynamicObject();
            auto* ob = b.getDynamicObject();
            if (oa == nullptr || ob == nullptr) { where = "object vs non-object"; return false; }
            if (oa->getProperties().size() != ob->getProperties().size())
            {
                where = "key count " + juce::String (oa->getProperties().size()) + " vs " + juce::String (ob->getProperties().size())
                        + ": " + juce::JSON::toString (a, true) + " | " + juce::JSON::toString (b, true);
                return false;
            }
            for (auto& p : oa->getProperties())
            {
                if (! ob->hasProperty (p.name)) { where = "missing key " + p.name.toString(); return false; }
                if (! same (p.value, ob->getProperty (p.name), where)) { where = p.name.toString() + "." + where; return false; }
            }
            return true;
        }
        if (a.isVoid() != b.isVoid()) { where = "null vs value"; return false; }
        if (a.isVoid()) return true;
        if (a.isString() != b.isString() || a.isBool() != b.isBool()) { where = "type"; return false; }
        if (a.isString()) { if (a.toString() != b.toString()) { where = "string value"; return false; } return true; }
        if (a.isBool()) { if ((bool) a != (bool) b) { where = "bool value"; return false; } return true; }
        if (std::abs ((double) a - (double) b) > 1e-9) { where = "number value"; return false; }
        return true;
    }

    template <typename T>
    void roundTrip (const char* label, const char* text)
    {
        beginTest (juce::String ("round trip: ") + label);
        const auto in = parse (text);
        T value;
        expect (json::decode (in, value), "decode failed");
        const auto out = json::encode (value);
        juce::String where;
        expect (same (in, out, where), where);
    }

    void runTest() override
    {
        roundTrip<UserPublic> ("UserPublic", R"({ "id": "u_1", "username": "chan_keys", "nickname": "승찬", "part": "keys" })");

        roundTrip<Me> ("Me", R"({
          "id": "u_1", "username": "chan_keys", "nickname": "승찬", "part": "keys",
          "email": "me@example.com", "email_verified": true, "pending_email": null,
          "settings": { "friend_requests": "everyone", "room_invites": "friends" },
          "created_at": "2026-10-13T07:33:00Z" })");

        roundTrip<Me> ("Me (signup, nulls)", R"({
          "id": "u_1", "username": "chan_keys", "nickname": null, "part": null,
          "email": "me@example.com", "email_verified": false, "pending_email": null,
          "settings": { "friend_requests": "everyone", "room_invites": "friends" },
          "created_at": "2026-10-13T07:33:00Z" })");

        roundTrip<UserProfile> ("UserProfile", R"({
          "user": { "id": "u_2", "username": "junho", "nickname": "준호", "part": "guitar" },
          "relation": "friend", "status": "in_room", "last_seen_at": null,
          "room": { "id": "r_1", "title": "준호의 새벽 합주", "joinable": true },
          "can_send_friend_request": false, "can_invite": true })");

        roundTrip<UserProfile> ("UserProfile (not friend)", R"({
          "user": { "id": "u_2", "username": "junho", "nickname": "준호", "part": "guitar" },
          "relation": "none", "status": null, "last_seen_at": null, "room": null,
          "can_send_friend_request": true, "can_invite": false })");

        static const char* room = R"({
          "id": "r_1", "type": "public", "entry_mode": "approval", "title": "준호의 새벽 합주",
          "description": "92bpm 발라드 위주", "capacity": 7, "member_count": 1, "host_id": "u_2",
          "host_present": true, "host_rtt_ms": 9,
          "members": [ { "user": { "id": "u_2", "username": "junho", "nickname": "준호", "part": "guitar" },
                         "is_host": true, "is_friend": false, "joined_at": "2026-10-13T07:33:00Z" } ],
          "my_join_request_until": null, "created_at": "2026-10-13T07:33:00Z" })";
        roundTrip<Room> ("Room", room);

        roundTrip<Room> ("Room (private, nulls)", R"({
          "id": "r_1", "type": "private", "entry_mode": null, "title": "t", "description": "",
          "capacity": 2, "member_count": 0, "host_id": "u_2", "host_present": false, "host_rtt_ms": null,
          "members": [], "my_join_request_until": "2026-10-13T07:38:00Z", "created_at": "2026-10-13T07:33:00Z" })");

        roundTrip<RoomDetail> ("RoomDetail", R"({
          "id": "r_1", "type": "private", "entry_mode": null, "title": "t", "description": "d",
          "capacity": 7, "member_count": 0, "host_id": "u_2", "host_present": false, "host_rtt_ms": null,
          "members": [], "my_join_request_until": null, "created_at": "2026-10-13T07:33:00Z",
          "invite_code": "A7K2QX",
          "connection": { "aoo_host": "aoo.meon.co.kr", "aoo_port": 10999, "group_name": "meon-r_1",
                          "group_password": "server-issued-random-secret", "aoo_username": "u_1",
                          "aoo_user_password": "server-issued-random-secret", "members": ["u_1", "u_2"] } })");

        roundTrip<FriendRequest> ("FriendRequest", R"({ "id": "fr_1",
          "from": { "id": "u_2", "username": "junho", "nickname": "준호", "part": "guitar" }, "created_at": "2026-10-13T07:33:00Z" })");

        roundTrip<Friend> ("Friend", R"({
          "user": { "id": "u_2", "username": "junho", "nickname": "준호", "part": "guitar" },
          "status": "offline", "last_seen_at": "2026-10-13T04:10:00Z", "room": null, "can_invite": false })");

        roundTrip<Invite> ("Invite", R"({ "id": "inv_1",
          "from": { "id": "u_2", "username": "junho", "nickname": "준호", "part": "guitar" },
          "room": { "id": "r_1", "title": "준호의 새벽 합주" }, "created_at": "2026-10-13T07:33:00Z" })");

        roundTrip<JoinRequest> ("JoinRequest", R"({ "id": "jr_1", "room_id": "r_1",
          "user": { "id": "u_2", "username": "junho", "nickname": "준호", "part": "guitar" },
          "via_invite": false, "created_at": "2026-10-13T07:33:00Z", "expires_at": "2026-10-13T07:34:00Z" })");

        roundTrip<Block> ("Block", R"({ "user": { "id": "u_2", "username": "raon", "nickname": "라온", "part": "guitar" },
          "created_at": "2026-10-13T07:33:00Z" })");

        roundTrip<Config> ("Config", R"({ "min_build": 51, "latest_build": 58, "latency_coefficient": 0.7, "discord_url": "https://discord.gg/x" })");

        roundTrip<Hello> ("Hello", R"({
          "me": { "id": "u_1", "username": "chan_keys", "nickname": "승찬", "part": "keys", "email": "me@example.com",
                  "email_verified": true, "pending_email": null,
                  "settings": { "friend_requests": "everyone", "room_invites": "friends" }, "created_at": "2026-10-13T07:33:00Z" },
          "current_room": null, "join_requests": [], "friends": [], "has_unread_notifications": true,
          "pending_invites": [], "config": { "min_build": 51, "latest_build": 58, "latency_coefficient": 0.7, "discord_url": "u" } })");

        beginTest ("missing required field fails");
        {
            UserPublic u;
            expect (! json::decode (parse (R"({ "id": "u_1", "nickname": "a", "part": "keys" })"), u));
            Room r;
            expect (! json::decode (parse (R"({ "id": "r_1" })"), r));
            Config c;
            expect (! json::decode (parse (R"({ "min_build": "51", "latest_build": 1, "latency_coefficient": 0.7, "discord_url": "" })"), c));
        }

        beginTest ("patch bodies send only set fields");
        {
            MePatch p;
            p.friendRequests = juce::String ("none");
            expectEquals (juce::JSON::toString (json::encode (p), true), juce::String (R"({"settings": {"friend_requests": "none"}})"));
            RoomPatch rp;
            rp.type = juce::String ("private");
            rp.setEntryMode = true;
            expectEquals (juce::JSON::toString (json::encode (rp), true), juce::String (R"({"type": "private", "entry_mode": null})"));
            CreateRoom c;
            c.type = "private";
            c.title = "t";
            expect (! json::encode (c).hasProperty ("entry_mode"));
        }

        beginTest ("error body");
        {
            auto e = json::decodeError (409, parse (R"({ "error": { "code": "room_full", "message": "방이 가득 찼어요" } })"));
            expect (e.code == ErrorCode::roomFull);
            expectEquals (e.httpStatus, 409);
            auto f = json::decodeError (400, parse (R"({ "error": { "code": "invalid_request", "message": "", "field": "password" } })"));
            expectEquals (f.field, juce::String ("password"));
            expect (json::decodeError (418, parse (R"({ "error": { "code": "teapot", "message": "" } })")).code == ErrorCode::unknown);
            int n = 0;
            auto* table = errorTable (n);
            for (int i = 0; i < n; ++i)
                expect (errorFromKey (table[i].key) == table[i].code);
            expectEquals (errorHttpStatus (ErrorCode::roomClosed), 410);
            expectEquals (errorHttpStatus (ErrorCode::targetInRoom), 409);
        }

        beginTest ("ISO 8601 time");
        {
            const juce::int64 t = 1791876780000;   // 2026-10-13T07:33:00Z
            expectEquals (json::isoTime (t), juce::String ("2026-10-13T07:33:00Z"));
            expectEquals (json::parseIsoTime ("2026-10-13T07:33:00Z"), t);
            expectEquals (json::parseIsoTime ("2026-10-13T07:33:00.250Z"), t + 250);
            expectEquals (json::isoTime (0), juce::String ("1970-01-01T00:00:00Z"));
            expectEquals (json::isoTime (951782400000), juce::String ("2000-02-29T00:00:00Z"));
            expectEquals (json::parseIsoTime ("garbage"), (juce::int64) 0);
        }

        beginTest ("realtime event decode");
        {
            RealtimeEvent e;
            expect (RealtimeEvent::decode (parse (R"({ "type": "room.closed", "seq": 3, "data": { "room_id": "r_1", "reason": "host_closed" } })"), e));
            expect (e.kind == RealtimeEvent::Kind::roomClosed);
            expectEquals (e.seq, 3);
            expectEquals (e.reason, juce::String ("host_closed"));
            expect (RealtimeEvent::decode (parse (R"({ "type": "invite.expired", "seq": 4, "data": { "invite_id": "inv_1" } })"), e));
            expectEquals (e.inviteId, juce::String ("inv_1"));
            expect (! RealtimeEvent::decode (parse (R"({ "type": "something.new", "seq": 5, "data": {} })"), e));
            expect (! RealtimeEvent::decode (parse (R"({ "type": "rooms.added", "seq": 6, "data": { "id": "r_1" } })"), e));
        }
    }
};

static ApiJsonTests apiJsonTests;

} // namespace meon::core
