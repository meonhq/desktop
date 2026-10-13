// HomeController: 공개 방 목록·구독, 바로 입장 / 승인 요청 / 코드 입장 / 방 만들기, 입장 실패 분류,
// 친구 요청 결과, 받은 요청 수락, 초대 입장, 알림 읽음
#include "../core/home/HomeController.h"
#include "../core/model/RoomFilter.h"
#include "../core/auth/AuthController.h"
#include "../core/api/fake/FakeMeonServer.h"
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

            expect (HomeController::classify (ApiError::of (ErrorCode::network)) == F::network);
            expect (HomeController::classify (ApiError::of (ErrorCode::clientOutdated)) == F::version);
            expect (HomeController::classify (ApiError::of (ErrorCode::hostAbsent)) == F::hostAbsent);
            expect (HomeController::classify (ApiError::of (ErrorCode::kicked)) == F::kicked);
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
