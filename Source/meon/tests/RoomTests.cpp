// RoomController (방장 명령·입장 요청) · AccountController (프로필·설정·비밀번호·이메일·차단·탈퇴)
#include "../core/room/RoomController.h"
#include "../core/account/AccountController.h"
#include "../core/home/HomeController.h"
#include "../core/api/fake/FakeMeonServer.h"
#include "MeonFixtures.h"

namespace meon::core
{

class RoomTests : public juce::UnitTest
{
public:
    RoomTests() : juce::UnitTest ("RoomController, AccountController", "Room") {}

    struct Rig
    {
        MemoryTokenStore tokens;
        ManualScheduler clock;
        FakeMeonServer server { clock, [] { FakeMeonServer::Options o; o.latencyMs = 10; o.scripts = false; return o; }() };
        MeonApi api { server, 0 };
        Realtime rt { server, clock, 0 };
        AppStore store;
        AuthController auth { api, rt, store, tokens, clock };
        HomeController home { api, rt, store, clock };
        RoomController room { api, store };
        AccountController account { api, store, auth };

        Rig()
        {
            server.loadWorld (juce::JSON::parse (juce::String::fromUTF8 (MeonFixtures::world_json, MeonFixtures::world_jsonSize)));
            rt.addListener (&store);
            auth.start();
            wait();
            auth.showSignup();
            auth.signup ("room@example.com", "room_tester", "abcd123!", true, [] (Result<Done>) {});
            wait();
            auth.verify (server.getLastMailedCode(), [] (Result<Done>) {});
            wait();
            auth.setNickname ("tester", [] (Result<Done>) {});
            wait();
            auth.setPart ("bass", [] (Result<Done>) {});
            wait();
            auth.finishDiscord();
            auth.finishDone();
        }
        ~Rig() { rt.removeListener (&store); }
        void wait (int ms = 200) { clock.advance (ms); }
    };

    void runTest() override
    {
        beginTest ("host: patch room, join request accept, close room");
        {
            Rig r;
            CreateRoom c;
            c.type = "public";
            c.entryMode = juce::String ("approval");
            c.title = "host room";
            c.capacity = 5;
            r.home.createRoom (c, [] (Result<RoomDetail>) {});
            r.wait();
            expect (r.store.state().amHost());

            RoomPatch p;
            p.title = juce::String ("renamed");
            p.capacity = 4;
            bool ok = false;
            r.room.patchRoom (p, [&] (Result<RoomDetail> x) { ok = x.ok(); });
            r.wait();
            expect (ok);
            expectEquals (r.store.state().currentRoom->title, juce::String ("renamed"));
            expectEquals (r.store.state().currentRoom->capacity, 4);

            r.server.devJoinRequest();
            r.wait();
            expectEquals ((int) r.store.state().joinRequests.size(), 1);
            const auto rid = r.store.state().joinRequests.front().id;
            const auto who = r.store.state().joinRequests.front().user.id;
            r.room.acceptJoinRequest (rid);
            r.wait();
            expect (r.store.state().joinRequests.empty());
            bool member = false;
            for (auto& m : r.store.state().currentRoom->members) if (m.user.id == who) member = true;
            expect (member);

            std::vector<ErrorCode> failures;
            r.room.onActionFailed = [&] (ErrorCode c) { failures.push_back (c); };
            r.room.kick (who);
            r.wait();
            member = false;
            for (auto& m : r.store.state().currentRoom->members) if (m.user.id == who) member = true;
            expect (! member);
            expect (failures.empty());

            // 이미 나간 멤버를 다시 내보내기·방장 넘기기: target_not_in_room → 화면에 알린다 (R11 PR3)
            r.room.kick (who);
            r.wait();
            r.room.transferHost (who);
            r.wait();
            expect (failures == std::vector<ErrorCode> { ErrorCode::targetNotInRoom, ErrorCode::targetNotInRoom });
            r.room.onActionFailed = nullptr;

            r.room.closeRoom();
            r.wait();
            expect (! r.store.state().inRoom());
            expect (r.room.takeClosedByMe());
            expect (! r.room.takeClosedByMe());
        }

        beginTest ("host: decline a join request from a list row that the store change deletes: the right id reaches the server");
        {
            // 합주 화면 입장 요청 목록(JoinRequestList) 흉내: 요청이 바뀌면 줄을 모두 지우고 다시 만든다 (예전에는 지워진 줄의 id 를 보냈다)
            Rig r;
            CreateRoom c;
            c.type = "public";
            c.entryMode = juce::String ("approval");
            c.title = "decline room";
            r.home.createRoom (c, [] (Result<RoomDetail>) {});
            r.wait();
            r.server.devJoinRequest();
            r.wait();
            r.server.devJoinRequest();
            r.wait();
            struct Row { JoinRequest req; };
            struct List : AppStore::Listener
            {
                explicit List (AppStore& s) : store (s) { store.addListener (this); rebuild(); }
                ~List() override { store.removeListener (this); }
                void rebuild()
                {
                    rows.clear();
                    for (auto& q : store.state().joinRequests) rows.push_back (std::make_unique<Row> (Row { q }));
                }
                void stateChanged (StateKey k) override { if (k == StateKey::joinRequests) rebuild(); }
                AppStore& store;
                std::vector<std::unique_ptr<Row>> rows;
            } list (r.store);
            expectEquals ((int) list.rows.size(), 2);
            juce::StringArray paths;
            r.server.onRequestForTest = [&] (const HttpRequest& q) { if (q.path.endsWith ("/decline")) paths.add (q.path); };
            const auto roomId = r.store.state().currentRoom->id;
            const auto firstId = list.rows[0]->req.id;
            r.room.declineJoinRequest (list.rows[0]->req.id);   // 줄이 지워지는 중에 이 id 를 읽는다
            expectEquals (paths.size(), 1);
            expectEquals (paths[0], "/rooms/" + roomId + "/join-requests/" + firstId + "/decline");
            r.wait();
            expectEquals ((int) r.store.state().joinRequests.size(), 1);
            r.server.onRequestForTest = nullptr;
        }

        beginTest ("account: nickname, part, privacy, password");
        {
            Rig r;
            r.account.setNickname ("newname");
            r.account.setPart ("drums");
            r.account.setFriendRequests ("none");
            r.account.setRoomInvites ("none");
            r.wait();
            const auto& me = *r.store.state().me;
            expectEquals (me.nickname.value_or (""), juce::String ("newname"));
            expectEquals (me.part.value_or (""), juce::String ("drums"));
            expectEquals (me.settings.friendRequests, juce::String ("none"));
            expectEquals (me.settings.roomInvites, juce::String ("none"));

            std::optional<ErrorCode> code;
            r.account.changePassword ("wrong-pass1!", "newpass12!", [&] (Result<Done> x) { if (! x.ok()) code = x.code(); });
            r.wait();
            expect (code == ErrorCode::invalidCredentials);
            bool ok = false;
            r.account.changePassword ("abcd123!", "newpass12!", [&] (Result<Done> x) { ok = x.ok(); });
            r.wait();
            expect (ok);
        }

        beginTest ("account: email change needs the current password, then a code (API 4.4)");
        {
            Rig r;
            r.wait (31000);   // 가입 인증 메일 뒤 재전송 간격 (30초)
            std::optional<ErrorCode> wrong;
            r.account.requestEmailChange ("new@example.com", "wrong-pass1!", [&] (Result<Done> x) { if (! x.ok()) wrong = x.code(); });
            r.wait();
            expect (wrong == ErrorCode::invalidCredentials, "틀린 비밀번호");
            bool sent = false;
            r.account.requestEmailChange ("new@example.com", "abcd123!", [&] (Result<Done> x) { sent = x.ok(); });
            r.wait();
            expect (sent);
            bool verified = false;
            r.account.verifyEmailChange (r.server.getLastMailedCode(), [&] (Result<Me> x) { verified = x.ok(); });
            r.wait();
            expect (verified);
            expectEquals (r.store.state().me->email, juce::String ("new@example.com"));
        }

        beginTest ("account: block, list, unblock; profile relation");
        {
            Rig r;
            juce::String stranger;
            for (auto& m : r.store.state().publicRooms) juce::ignoreUnused (m);
            r.home.setHomeVisible (true);
            r.wait();
            for (auto& room : r.store.state().publicRooms)
                for (auto& m : room.members)
                    if (! m.isFriend && stranger.isEmpty()) stranger = m.user.id;
            expect (stranger.isNotEmpty());

            std::optional<UserProfile> prof;
            r.account.loadProfile (stranger, [&] (Result<UserProfile> x) { if (x.ok()) prof = x.get(); });
            r.wait();
            expect (prof && prof->relation == "none");

            r.account.block (stranger);
            r.wait();
            std::vector<Block> blocks;
            r.account.listBlocks ([&] (Result<std::vector<Block>> x) { if (x.ok()) blocks = x.get(); });
            r.wait();
            bool found = false;
            for (auto& b : blocks) if (b.user.id == stranger) found = true;
            expect (found);

            r.account.unblock (stranger);
            r.wait();
            r.account.listBlocks ([&] (Result<std::vector<Block>> x) { if (x.ok()) blocks = x.get(); });
            r.wait();
            found = false;
            for (auto& b : blocks) if (b.user.id == stranger) found = true;
            expect (! found);
        }

        beginTest ("account: delete returns to login");
        {
            Rig r;
            bool ok = false;
            r.account.deleteAccount ([&] (Result<Done> x) { ok = x.ok(); });
            r.wait();
            expect (ok);
            expect (r.auth.step() == AuthController::Step::login);
        }
    }
};

static RoomTests roomTests;

} // namespace meon::core
