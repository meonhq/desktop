// HomeController: 공개 방 목록·구독, 바로 입장 / 승인 요청 / 코드 입장 / 방 만들기, 입장 실패 분류,
// 친구 요청 결과, 받은 요청 수락, 초대 입장, 알림 읽음
#include "../core/home/HomeController.h"
#include "../core/model/RoomFilter.h"
#include "../core/auth/AuthController.h"
#include "../core/api/fake/FakeMeonServer.h"
#include "../core/api/ApiJson.h"
#include "MeonFixtures.h"

namespace meon::core
{

class HomeTests : public juce::UnitTest
{
public:
    HomeTests() : juce::UnitTest ("HomeController", "Home") {}

    struct Rig
    {
        MemoryTokenStore tokens;
        ManualScheduler clock;
        FakeMeonServer server { clock, [] { FakeMeonServer::Options o; o.latencyMs = 10; o.scripts = false; o.fakeHostsAccept = false; return o; }() };
        MeonApi api { server, 0 };
        Realtime rt { server, clock, 0 };
        AppStore store;
        AuthController auth { api, rt, store, tokens, clock };
        HomeController home { api, rt, store, clock };

        Rig()
        {
            server.loadWorld (juce::JSON::parse (juce::String::fromUTF8 (MeonFixtures::world_json, MeonFixtures::world_jsonSize)));
            rt.addListener (&store);
            auth.start();
            wait();
            auth.showSignup();
            auth.signup ("home@example.com", "home_tester", "abcd123!", true, [] (Result<Done>) {});
            wait();
            auth.verify (server.getLastMailedCode(), [] (Result<Done>) {});
            wait();
            auth.setNickname ("tester", [] (Result<Done>) {});
            wait();
            auth.setPart ("guitar", [] (Result<Done>) {});
            wait();
            auth.finishDiscord();
            auth.finishDone();
        }
        ~Rig() { rt.removeListener (&store); }
        void wait (int ms = 200) { clock.advance (ms); }

        const Room* findRoom (std::function<bool (const Room&)> pred) const
        {
            for (auto& r : store.state().publicRooms)
                if (pred (r)) return &r;
            return nullptr;
        }
    };

    void runTest() override
    {
        using F = HomeController::JoinFailure;

        beginTest ("home visible loads rooms, subscribes, loads friend requests");
        {
            Rig r;
            expect (r.auth.step() == AuthController::Step::ready);
            r.home.setHomeVisible (true);
            r.wait();
            expect (! r.store.state().publicRooms.empty());
            expectEquals ((int) r.store.state().friendRequests.size(), 2);
            r.home.setHomeVisible (false);
        }

        beginTest ("instant join sets current room; leave clears it");
        {
            Rig r;
            r.home.setHomeVisible (true);
            r.wait();
            const auto myId = r.store.state().me->id;
            auto* room = r.findRoom ([&] (const Room& x) { return ! x.isApproval() && rooms::hasRoomFor (x, myId); });
            expect (room != nullptr);
            const auto id = room->id;
            bool ok = false;
            r.home.joinRoom (id, [&] (Result<JoinResult> x) { ok = x.ok() && x.get().room.has_value(); });
            r.wait();
            expect (ok);
            expect (r.store.state().currentRoom && r.store.state().currentRoom->id == id);
            r.home.leaveRoom();
            expect (! r.store.state().currentRoom);
            r.wait();
        }

        beginTest ("approval room: request pending until request_until");
        {
            Rig r;
            r.home.setHomeVisible (true);
            r.wait();
            const auto myId = r.store.state().me->id;
            auto* room = r.findRoom ([&] (const Room& x) { return x.isApproval() && x.hostPresent && rooms::hasRoomFor (x, myId); });
            expect (room != nullptr);
            const auto id = room->id;
            bool requested = false;
            r.home.joinRoom (id, [&] (Result<JoinResult> x) { requested = x.ok() && x.get().requested(); });
            r.wait();
            expect (requested);
            expect (! r.store.state().currentRoom);
            auto* after = r.findRoom ([&] (const Room& x) { return x.id == id; });
            expect (after != nullptr && r.home.isRequestPending (*after));
            r.wait (5 * 60 * 1000 + 1000);
            after = r.findRoom ([&] (const Room& x) { return x.id == id; });
            expect (after != nullptr && ! r.home.isRequestPending (*after));
        }

        beginTest ("friend button: join / request / requested shared with the room card, follows entry mode (bug 25)");
        {
            using J = HomeController::FriendJoin;
            Rig r;
            r.home.setHomeVisible (true);
            r.wait();
            // 유나(u_f_yuna)는 승인 방 r_f_2 에 있다
            const Friend* yuna = r.store.state().findFriend ("u_f_yuna");
            expect (yuna != nullptr && yuna->room && yuna->room->id == "r_f_2");
            if (yuna == nullptr) return;
            const auto fr = *yuna;
            expect (r.home.friendJoinState (fr) == J::request);

            // 친구 목록에서 요청 → "요청 보냄", 같은 방 카드도 요청 보냄
            bool requested = false;
            r.home.joinFriend (fr, [&] (Result<JoinResult> x) { requested = x.ok() && x.get().requested(); });
            r.wait();
            expect (requested);
            expect (r.home.friendJoinState (fr) == J::requested);
            const auto* card = r.store.state().findPublicRoom ("r_f_2");
            expect (card != nullptr && r.home.isRequestPending (*card));
            // "요청 보냄" 동안은 다시 보내지 않는다
            bool called = false;
            r.home.joinFriend (fr, [&] (Result<JoinResult>) { called = true; });
            r.wait();
            expect (! called);

            // 방의 입장 방식이 바로 입장으로 바뀌면 바로 "참여"
            auto changed = *card;
            changed.entryMode = juce::String ("instant");
            r.store.upsertPublicRoom (changed);
            expect (r.home.friendJoinState (fr) == J::join);
            changed.entryMode = juce::String ("approval");
            r.store.upsertPublicRoom (changed);
            expect (r.home.friendJoinState (fr) == J::requested);

            // 5분이 지나면 다시 "요청" (거절·만료를 구분하지 않는다)
            r.wait (5 * 60 * 1000 + 1000);
            expect (r.home.friendJoinState (fr) == J::request);

            // 카드에서 요청해도 친구 쪽이 "요청 보냄"
            r.home.joinRoom ("r_f_2", [] (Result<JoinResult>) {});
            r.wait();
            expect (r.home.friendJoinState (fr) == J::requested);

            // 프로필 창(버그 27)도 같은 상태: RoomRef 로 묻고, "요청 보냄" 동안은 콜백도 없이 아무것도 안 한다
            expect (r.home.roomJoinState (*fr.room) == J::requested);
            bool profileCalled = false;
            r.home.joinFriendRoom (*fr.room, [&] (Result<JoinResult>) { profileCalled = true; });
            r.wait();
            expect (! profileCalled);

            // 비공개 방이나 정원이 찬 방(room 이 없거나 joinable false)은 버튼 없음
            auto hidden = fr;
            hidden.room.reset();
            expect (r.home.friendJoinState (hidden) == J::none);
            auto fullRoom = fr;
            fullRoom.room->joinable = false;
            expect (r.home.friendJoinState (fullRoom) == J::none);
            expect (r.home.roomJoinState (*fullRoom.room) == J::none);
        }

        beginTest ("full room and codes map to failure kinds");
        {
            Rig r;
            r.home.setHomeVisible (true);
            r.wait();
            const auto myId = r.store.state().me->id;
            auto* full = r.findRoom ([&] (const Room& x) { return ! rooms::hasRoomFor (x, myId); });
            expect (full != nullptr);
            std::optional<ApiError> err;
            r.home.joinRoom (full->id, [&] (Result<JoinResult> x) { if (! x.ok()) err = x.err(); });
            r.wait();
            expect (err && HomeController::classify (*err) == F::full);

            err.reset();
            r.home.joinByCode ("full77", [&] (Result<JoinResult> x) { if (! x.ok()) err = x.err(); });
            r.wait();
            expect (err && HomeController::classify (*err) == F::full);

            err.reset();
            r.home.joinByCode ("CLSD99", [&] (Result<JoinResult> x) { if (! x.ok()) err = x.err(); });
            r.wait();
            expect (err && HomeController::classify (*err) == F::closed);

            err.reset();
            r.home.joinByCode ("ABCDE0", [&] (Result<JoinResult> x) { if (! x.ok()) err = x.err(); });
            r.wait();
            expect (err && err->code == ErrorCode::invalidCode && HomeController::classify (*err) == F::closed);

            expect (HomeController::classify (ApiError::of (ErrorCode::network)) == F::server);
            expect (HomeController::classify (ApiError::of (ErrorCode::clientOutdated)) == F::version);
            expect (HomeController::classify (ApiError::of (ErrorCode::hostAbsent)) == F::hostAbsent);
            expect (HomeController::classify (ApiError::of (ErrorCode::kicked)) == F::kicked);
            // API 5.5 표 전체 (R11 PR3): 가득 참 / 방이 닫혔어요 / 연결 실패 / 426 은 강제 업데이트 화면(창은 띄우지 않음)
            expect (HomeController::classify (ApiError::of (ErrorCode::roomFull)) == F::full);
            for (auto c : { ErrorCode::roomClosed, ErrorCode::roomNotFound, ErrorCode::invalidCode })
                expect (HomeController::classify (ApiError::of (c)) == F::closed);
            for (auto c : { ErrorCode::network, ErrorCode::timeout, ErrorCode::internal })
                expect (HomeController::classify (ApiError::of (c)) == F::server);
            expect (HomeController::classify (ApiError::of (ErrorCode::alreadyInRoom)) == F::alreadyInRoom);
        }

        beginTest ("create private room enters it with a code; second create is already_in_room");
        {
            Rig r;
            CreateRoom c;
            c.type = "private";
            c.title = "test room";
            c.capacity = 4;
            bool ok = false;
            r.home.createRoom (c, [&] (Result<RoomDetail> x) { ok = x.ok(); });
            r.wait();
            expect (ok);
            expect (r.store.state().currentRoom && r.store.state().currentRoom->inviteCode.has_value());
            expect (r.store.state().amHost());

            std::optional<ErrorCode> code;
            r.home.createRoom (c, [&] (Result<RoomDetail> x) { if (! x.ok()) code = x.code(); });
            r.wait();
            expect (code == ErrorCode::alreadyInRoom);
        }

        beginTest ("friend request results");
        {
            Rig r;
            auto send = [&] (const juce::String& name)
            {
                juce::String result;
                r.home.sendFriendRequest (name, [&] (Result<juce::String> x) { result = x.ok() ? x.get() : "error:" + errorKey (x.code()); });
                r.wait();
                return result;
            };
            expectEquals (send ("@TAEO"), juce::String ("already_friends"));
            expectEquals (send ("nobody_here"), juce::String ("not_found"));
            expectEquals (send ("home_tester"), juce::String ("self"));
            expectEquals (send ("minjun"), juce::String ("sent"));
            expectEquals (send ("minjun"), juce::String ("already_requested"));
        }

        beginTest ("accept friend request, notifications read");
        {
            Rig r;
            r.home.setHomeVisible (true);
            r.wait();
            const auto friendsBefore = r.store.state().friends.size();
            const auto reqId = r.store.state().friendRequests.front().id;
            r.home.acceptFriendRequest (reqId);
            expectEquals ((int) r.store.state().friendRequests.size(), 1);
            r.wait();
            expectEquals ((int) r.store.state().friends.size(), (int) friendsBefore + 1);

            r.server.devFriendRequest();
            r.wait();
            expect (r.store.state().hasUnreadNotifications);
            r.home.markNotificationsRead();
            expect (! r.store.state().hasUnreadNotifications);
        }

        beginTest ("friend request accept/decline from a list row that the store change deletes: the right id reaches the server (no use-after-free)");
        {
            // 알림 목록(NotificationList) 흉내: 받은 요청이 바뀌면 줄을 모두 지우고 다시 만든다. 줄이 자기 멤버의 id 로
            // 수락·거절을 부른다 (예전에는 참조로 넘겨, 지워진 줄의 id 를 서버로 보냈다)
            Rig r;
            r.home.setHomeVisible (true);
            r.wait();
            struct Row { FriendRequest request; };
            struct List : AppStore::Listener
            {
                explicit List (AppStore& s) : store (s) { store.addListener (this); rebuild(); }
                ~List() override { store.removeListener (this); }
                void rebuild()
                {
                    rows.clear();
                    for (auto& fr : store.state().friendRequests) rows.push_back (std::make_unique<Row> (Row { fr }));
                }
                void stateChanged (StateKey k) override { if (k == StateKey::friendRequests) rebuild(); }
                AppStore& store;
                std::vector<std::unique_ptr<Row>> rows;
            } list (r.store);
            juce::StringArray paths;
            r.server.onRequestForTest = [&] (const HttpRequest& q) { if (q.path.startsWith ("/friends/requests/")) paths.add (q.method + " " + q.path); };
            expectEquals ((int) list.rows.size(), 2);
            const auto firstId = list.rows[0]->request.id, secondId = list.rows[1]->request.id;
            const auto friendsBefore = (int) r.store.state().friends.size();

            r.home.acceptFriendRequest (list.rows[0]->request.id);   // 줄이 지워지는 중에 이 id 를 읽는다
            expectEquals (paths.size(), 1);
            expectEquals (paths[0], "POST /friends/requests/" + firstId + "/accept");
            r.wait();
            expectEquals ((int) r.store.state().friends.size(), friendsBefore + 1);

            expectEquals ((int) list.rows.size(), 1);
            r.home.declineFriendRequest (list.rows[0]->request.id);
            expectEquals (paths.size(), 2);
            expectEquals (paths[1], "POST /friends/requests/" + secondId + "/decline");
            r.wait();
            expect (r.store.state().friendRequests.empty());
            r.server.onRequestForTest = nullptr;
        }

        beginTest ("friend request accept/decline failure: the request comes back in place and the screen is told; not_found (handled elsewhere) is not a failure");
        {
            Rig r;
            r.home.setHomeVisible (true);
            r.wait();
            juce::Array<ErrorCode> failed;
            r.home.onFriendRequestFailed = [&] (ErrorCode c) { failed.add (c); };
            auto ids = [&] { juce::StringArray a; for (auto& f : r.store.state().friendRequests) a.add (f.id); return a; };
            const auto before = ids();
            expectEquals (before.size(), 2);

            // 네트워크 오류로 수락 실패: 바로 빠졌다가 같은 자리(두 번째)로 돌아온다
            r.server.replyForTest = [] (const HttpRequest& q) -> std::optional<HttpResponse>
            {
                if (q.path.startsWith ("/friends/requests/")) return HttpResponse {};
                return std::nullopt;
            };
            r.home.acceptFriendRequest (before[1]);
            expectEquals (ids().size(), 1);
            r.wait();
            expect (ids() == before, "수락 실패: 같은 자리로 되돌림");
            expectEquals (failed.size(), 1);
            expect (failed[0] == ErrorCode::network);

            // 서버 오류(500)로 거절 실패
            r.server.replyForTest = [] (const HttpRequest& q) -> std::optional<HttpResponse>
            {
                if (q.path.startsWith ("/friends/requests/")) return HttpResponse { 500, json::encodeError (ApiError::of (ErrorCode::internal)) };
                return std::nullopt;
            };
            r.home.declineFriendRequest (before[0]);
            expectEquals (ids().size(), 1);
            r.wait();
            expect (ids() == before, "거절 실패: 같은 자리로 되돌림");
            expectEquals (failed.size(), 2);
            expect (failed[1] == ErrorCode::internal);
            r.server.replyForTest = nullptr;

            // 다른 기기에서 먼저 처리함 → 이 기기에서 누르면 서버가 404 not_found. 실패로 보지 않는다 (되돌리지 않고 알리지 않음)
            r.api.acceptFriendRequest (before[0], [] (Result<Done>) {});
            r.api.declineFriendRequest (before[1], [] (Result<Done>) {});
            r.wait();
            expectEquals (ids().size(), 2, "다른 기기의 처리는 이 기기 목록에 오지 않는다");
            int notFound = 0;
            auto count404 = [&] (Result<Done> x) { if (! x.ok() && x.code() == ErrorCode::notFound) ++notFound; };
            r.home.acceptFriendRequest (before[0], count404);
            r.home.declineFriendRequest (before[1], count404);
            r.wait();
            expectEquals (notFound, 2, "서버가 not_found 로 답함");
            expect (r.store.state().friendRequests.empty(), "not_found: 되돌리지 않음");
            expectEquals (failed.size(), 2, "not_found: 알리지 않음");
            r.home.onFriendRequestFailed = nullptr;
        }

        beginTest ("accept: the new friend shows from a friends refresh even without friend.added; with both (either order), once");
        {
            Rig r;
            r.home.setHomeVisible (true);
            r.wait();
            r.server.devFriendRequest();
            r.wait();
            const auto reqs = r.store.state().friendRequests;
            expectEquals ((int) reqs.size(), 3);
            auto count = [&] (const juce::String& userId)
            {
                int n = 0;
                for (auto& f : r.store.state().friends) if (f.user.id == userId) ++n;
                return n;
            };
            const int friendsBefore = (int) r.store.state().friends.size();
            int friendLists = 0;
            r.server.onRequestForTest = [&] (const HttpRequest& q) { if (q.method == "GET" && q.path.upToFirstOccurrenceOf ("?", false, false) == "/friends") ++friendLists; };

            // friend.added 를 놓침: 수락 뒤 다시 받은 목록으로 나타난다
            r.server.dropEventForTest = [] (const juce::String& type, const juce::var&) { return type == "friend.added"; };
            r.home.acceptFriendRequest (reqs[0].id);
            r.wait();
            expectEquals (friendLists, 1, "수락 성공 뒤 친구 목록을 다시 받음");
            expectEquals (count (reqs[0].from.id), 1, "friend.added 없이도 나타남");

            // friend.added 가 먼저 오고 목록이 뒤에 와도 한 번
            r.server.dropEventForTest = nullptr;
            r.home.acceptFriendRequest (reqs[1].id);
            r.wait();
            expectEquals (friendLists, 2);
            expectEquals (count (reqs[1].from.id), 1, "friend.added + 목록: 한 번");

            // friend.added 가 목록보다 늦게 와도 한 번
            std::vector<juce::var> held;
            r.server.dropEventForTest = [&] (const juce::String& type, const juce::var& data)
            {
                if (type != "friend.added") return false;
                held.push_back (data);
                return true;
            };
            r.home.acceptFriendRequest (reqs[2].id);
            r.wait();
            expectEquals (count (reqs[2].from.id), 1);
            expectEquals ((int) held.size(), 1);
            RealtimeEvent late;
            expect (! held.empty() && RealtimeEvent::decode (RealtimeEvent::encode ("friend.added", 999, held.front()), late));
            r.store.event (late);
            expectEquals (count (reqs[2].from.id), 1, "목록 + 늦은 friend.added: 한 번");
            expectEquals ((int) r.store.state().friends.size(), friendsBefore + 3);
            expect (r.store.state().friendRequests.empty());
            r.server.dropEventForTest = nullptr;
            r.server.onRequestForTest = nullptr;
        }

        beginTest ("reconnect: a friend and a request missed while disconnected show up (hello friends, GET /friends/requests)");
        {
            Rig r;
            r.home.setHomeVisible (true);
            r.wait();
            const auto reqs = r.store.state().friendRequests;
            expectEquals ((int) reqs.size(), 2);
            auto isFriend = [&] (const juce::String& userId)
            {
                for (auto& f : r.store.state().friends) if (f.user.id == userId) return true;
                return false;
            };
            expect (! isFriend (reqs[0].from.id));
            r.server.devDropLink (3);
            r.wait (50);
            expect (r.store.state().link != Realtime::Link::online);
            // 끊긴 사이: 다른 기기에서 요청 하나를 수락하고(friend.added 는 이 기기로 오지 않는다), 새 요청이 하나 온다
            r.api.acceptFriendRequest (reqs[0].id, [] (Result<Done>) {});
            r.server.devFriendRequest();
            r.wait (100);
            expect (! isFriend (reqs[0].from.id), "끊긴 사이에는 모른다");
            expectEquals ((int) r.store.state().friendRequests.size(), 2);
            for (int i = 0; i < 120 && r.store.state().link != Realtime::Link::online; ++i) r.wait (500);
            expect (r.store.state().link == Realtime::Link::online);
            r.wait();
            expect (isFriend (reqs[0].from.id), "재연결 뒤 놓친 친구가 나타남");
            juce::StringArray ids;
            for (auto& f : r.store.state().friendRequests) ids.add (f.id);
            expect (! ids.contains (reqs[0].id) && ids.contains (reqs[1].id) && ids.size() == 2, "받은 요청도 다시 받음: " + ids.joinIntoString (","));
        }

        beginTest ("leave as the last member of a public room: the room does not stay in the list, whatever order the list request, the leave and rooms.removed take");
        {
            // listFirst: 나가기 직전에 목록 요청이 이미 나가 있다 (최소화한 홈·재연결 등)
            struct Case { const char* name; int holdList, slowList; bool dropRemoved, listFirst; };
            const Case cases[] = {
                { "기본 순서", 0, 0, false, false },
                { "목록이 나가기보다 늦게 처리됨", 60, 0, false, false },
                { "목록이 나가기보다 먼저 처리되고 rooms.removed 뒤에 도착", 0, 100, false, true },
                { "rooms.removed 를 놓침 (구독이 늦게 등록)", 0, 0, true, false },
                { "rooms.removed 를 놓치고, 먼저 처리된 목록이 늦게 도착", 0, 100, true, true },
            };
            for (auto& c : cases)
            {
                const auto name = juce::String::fromUTF8 (c.name);
                Rig r;
                // AppRoot 흉내: 방에 있으면 홈을 숨기고, 방이 비면 그 자리에서(setCurrentRoom 안에서) 홈을 보인다
                struct Screen : AppStore::Listener
                {
                    Screen (AppStore& s, HomeController& h) : store (s), home (h) { store.addListener (this); home.setHomeVisible (! store.state().inRoom()); }
                    ~Screen() override { store.removeListener (this); }
                    void stateChanged (StateKey k) override { if (k == StateKey::currentRoom) home.setHomeVisible (! store.state().inRoom()); }
                    AppStore& store;
                    HomeController& home;
                } screen (r.store, r.home);
                r.wait();
                CreateRoom cr;
                cr.type = "public";
                cr.entryMode = juce::String ("instant");
                cr.title = "solo room";
                r.home.createRoom (cr, [] (Result<RoomDetail>) {});
                r.wait();
                expect (r.store.state().inRoom());
                const auto roomId = r.store.state().currentRoom->id;
                // 내가 나가기 전에 목록에 내 방이 보이는 경우 (최소화 등)
                r.store.upsertPublicRoom (*r.store.state().currentRoom);

                juce::StringArray order;   // 서버가 받은 순서·보낸 이벤트·앱이 목록을 바꾼 때
                r.server.onRequestForTest = [&] (const HttpRequest& q)
                {
                    if ((q.method == "GET" && q.path == "/rooms") || q.path.endsWith ("/leave")) order.add (q.method + " " + q.path.replace (roomId, "{id}"));
                };
                r.server.onEmitForTest = [&] (const juce::String& to, const juce::String& type, const juce::var&)
                {
                    if (type == "rooms.removed" && to == r.server.meId()) order.add (c.dropRemoved ? juce::String::fromUTF8 ("rooms.removed(놓침)") : juce::String ("rooms.removed"));
                };
                r.server.holdForTest = [&] (const HttpRequest& q) { return q.method == "GET" && q.path == "/rooms" ? c.holdList : 0; };
                r.server.slowForTest = [&] (const HttpRequest& q) { return q.method == "GET" && q.path == "/rooms" ? c.slowList : 0; };
                r.server.dropEventForTest = [&] (const juce::String& type, const juce::var&) { return c.dropRemoved && type == "rooms.removed"; };
                struct Watch : AppStore::Listener
                {
                    Watch (AppStore& s, juce::StringArray& o, const juce::String& id) : store (s), out (o), roomId (id) { store.addListener (this); }
                    ~Watch() override { store.removeListener (this); }
                    void stateChanged (StateKey k) override
                    {
                        if (k == StateKey::publicRooms) out.add (juce::String::fromUTF8 (store.state().findPublicRoom (roomId) != nullptr ? "목록: 방 있음" : "목록: 방 없음"));
                    }
                    AppStore& store;
                    juce::StringArray& out;
                    juce::String roomId;
                } watch (r.store, order, roomId);

                if (c.listFirst) r.home.refreshRooms();
                r.home.leaveRoom();
                expect (r.store.state().findPublicRoom (roomId) == nullptr, name + ": 누르자마자 목록에서 빠짐");
                r.wait (500);
                logMessage (name + ": " + order.joinIntoString (" → "));
                expect (r.store.state().findPublicRoom (roomId) == nullptr, name + ": 목록에 남지 않음");
                r.server.onRequestForTest = nullptr;
                r.server.onEmitForTest = nullptr;
                r.server.holdForTest = nullptr;
                r.server.slowForTest = nullptr;
                r.server.dropEventForTest = nullptr;
            }
        }

        beginTest ("minimized (home visible in a room): leave from the room bar and leave-then-proceed drop my last-member public room at once");
        {
            for (int variant = 0; variant < 2; ++variant)
            {
                Rig r;
                r.home.setHomeVisible (true);
                r.wait();
                CreateRoom cr;
                cr.type = "public";
                cr.entryMode = juce::String ("instant");
                cr.title = "mini solo";
                r.home.createRoom (cr, [] (Result<RoomDetail>) {});
                r.wait();
                const auto roomId = r.store.state().currentRoom->id;
                r.home.refreshRooms();   // 최소화: 홈이 보이며 목록에 내 방이 있다
                r.wait();
                expect (r.store.state().findPublicRoom (roomId) != nullptr, "최소화한 홈 목록에 내 방");
                int lists = 0;
                r.server.onRequestForTest = [&] (const HttpRequest& q) { if (q.method == "GET" && q.path == "/rooms") ++lists; };
                r.server.dropEventForTest = [] (const juce::String& type, const juce::var&) { return type == "rooms.removed"; };
                bool proceeded = false;
                if (variant == 0) r.home.leaveRoom();                                              // 막대의 나가기
                else              r.home.leaveRoomThen ([&] (Result<Done> x) { proceeded = x.ok(); });   // 나가고 들어가기·만들기
                expect (r.store.state().findPublicRoom (roomId) == nullptr, "바로 목록에서 빠짐");
                r.home.refreshRooms();   // 나가는 중의 목록 요청은 나가기 뒤로 미룬다
                expectEquals (lists, 0);
                r.wait();
                expect (variant == 0 || proceeded);
                expect (! r.store.state().inRoom());
                expectEquals (lists, 1, "나가기 응답 뒤 목록을 한 번 다시 받음");
                expect (r.store.state().findPublicRoom (roomId) == nullptr, "rooms.removed 를 놓쳐도 남지 않음");
                r.server.onRequestForTest = nullptr;
                r.server.dropEventForTest = nullptr;
            }
        }

        beginTest ("leave fails (network): the list is fetched again and the room shows if it is still open");
        {
            Rig r;
            r.home.setHomeVisible (true);
            r.wait();
            CreateRoom cr;
            cr.type = "public";
            cr.entryMode = juce::String ("instant");
            cr.title = "stuck room";
            r.home.createRoom (cr, [] (Result<RoomDetail>) {});
            r.wait();
            const auto roomId = r.store.state().currentRoom->id;
            r.server.replyForTest = [] (const HttpRequest& q) -> std::optional<HttpResponse>
            {
                if (q.path.endsWith ("/leave")) return HttpResponse {};
                return std::nullopt;
            };
            bool failed = false;
            r.home.leaveRoomThen ([&] (Result<Done> x) { failed = ! x.ok(); });
            expect (r.store.state().findPublicRoom (roomId) == nullptr);
            r.wait();
            expect (failed && r.store.state().inRoom(), "나가기 실패: 방에 그대로");
            expect (r.store.state().findPublicRoom (roomId) != nullptr, "다시 받은 목록에 남아 있는 방이 보임");
            r.server.replyForTest = nullptr;
        }

        beginTest ("joining a room that has closed (410): it leaves the list at once and the list is fetched again");
        {
            Rig r;
            r.home.setHomeVisible (true);
            r.wait();
            CreateRoom cr;
            cr.type = "public";
            cr.entryMode = juce::String ("instant");
            cr.title = "gone room";
            r.home.createRoom (cr, [] (Result<RoomDetail>) {});
            r.wait();
            const Room stale = *r.store.state().currentRoom;
            r.home.leaveRoom();   // 서버가 방을 닫는다
            r.wait();
            r.store.upsertPublicRoom (stale);   // rooms.removed 를 놓친 목록
            expect (r.store.state().findPublicRoom (stale.id) != nullptr);
            int lists = 0;
            r.server.onRequestForTest = [&] (const HttpRequest& q) { if (q.method == "GET" && q.path == "/rooms") ++lists; };
            r.server.slowForTest = [] (const HttpRequest& q) { return q.method == "GET" && q.path == "/rooms" ? 300 : 0; };
            std::optional<ErrorCode> code;
            r.home.joinRoom (stale.id, [&] (Result<JoinResult> x) { if (! x.ok()) code = x.code(); });
            r.wait (50);
            expect (code.has_value() && HomeController::classify (ApiError::of (*code)) == F::closed, "닫힌 방");
            expect (r.store.state().findPublicRoom (stale.id) == nullptr, "목록 응답을 기다리지 않고 바로 빠짐");
            expectEquals (lists, 1, "목록도 다시 받음");
            r.wait (400);
            expect (r.store.state().findPublicRoom (stale.id) == nullptr);
            r.server.onRequestForTest = nullptr;
            r.server.slowForTest = nullptr;
        }

        beginTest ("invite friend: pending 3s then sent; cancel inside 3s");
        {
            using P = HomeController::InviteState::Phase;
            Rig r;
            CreateRoom c;
            c.type = "private";
            c.title = "invite room";
            r.home.createRoom (c, [] (Result<RoomDetail>) {});
            r.wait();
            expect (r.store.state().inRoom());
            juce::String target, second;
            for (auto& f : r.store.state().friends)
                if (f.status == "online" && f.canInvite) { if (target.isEmpty()) target = f.user.id; else if (second.isEmpty()) second = f.user.id; }
            expect (target.isNotEmpty() && second.isNotEmpty());

            r.home.invite (target);
            expect (r.home.inviteState (target).phase == P::pending);
            r.wait (500);
            expect (r.home.inviteState (target).phase == P::pending);
            r.wait (3000);
            expect (r.home.inviteState (target).phase == P::sent);

            r.home.invite (second);
            r.wait (500);
            r.home.cancelInvite (second);
            expect (r.home.inviteState (second).phase == P::none);
            r.wait (4000);
            expect (r.home.inviteState (second).phase == P::none);

            r.home.leaveRoom();
            r.wait();
            expect (r.home.inviteState (target).phase == P::none);   // 방이 바뀌면 지운다
        }

        beginTest ("accept invite joins the room; dismiss removes it");
        {
            Rig r;
            r.server.devInviteMe();
            r.wait (4000);
            expectEquals ((int) r.store.state().invites.size(), 1);
            const auto inv = r.store.state().invites.front();
            bool done = false;
            r.home.acceptInvite (inv.id, [&] (Result<JoinResult> x) { done = x.ok(); });
            expect (r.store.state().invites.empty());
            r.wait();
            expect (done);
            expect (r.store.state().currentRoom.has_value());

            r.home.leaveRoom();
            r.wait();
            r.server.devInviteMe();
            r.wait (4000);
            if (! r.store.state().invites.empty())
            {
                r.home.dismissInvite (r.store.state().invites.front().id);
                expect (r.store.state().invites.empty());
            }
        }
    }
};

static HomeTests homeTests;

} // namespace meon::core
