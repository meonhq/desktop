// 가짜 서버의 연출(다른 사용자 각본)을 가짜 시계로 몇 시간치 돌려도 죽지 않는지.
// 연출이 방을 지울 때 지운 방의 필드를 다시 읽던 문제(use-after-free)의 회귀 테스트이기도 하다.
#include "../core/home/HomeController.h"
#include "../core/room/RoomController.h"
#include "../core/auth/AuthController.h"
#include "../core/api/fake/FakeMeonServer.h"
#include "MeonFixtures.h"

#include <set>

namespace meon::core
{

class ScriptSoakTests : public juce::UnitTest
{
public:
    ScriptSoakTests() : juce::UnitTest ("FakeMeonServer scripts soak", "Fake") {}

    struct Rig
    {
        MemoryTokenStore tokens;
        ManualScheduler clock;
        FakeMeonServer server;
        MeonApi api { server, 0 };
        Realtime rt { server, clock, 0 };
        AppStore store;
        AuthController auth { api, rt, store, tokens, clock };
        HomeController home { api, rt, store, clock };

        explicit Rig (juce::uint32 seed)
            : server (clock, [seed] { FakeMeonServer::Options o; o.latencyMs = 10; o.scripts = true; o.scriptSpeed = 4.0; o.seed = seed; return o; }())
        {
            server.loadWorld (juce::JSON::parse (juce::String::fromUTF8 (MeonFixtures::world_json, MeonFixtures::world_jsonSize)));
            rt.addListener (&store);
            auth.start();
            wait();
            auth.showSignup();
            auth.signup ("soak@example.com", "soak_tester", "abcd123!", true, [] (Result<Done>) {});
            wait();
            auth.verify (server.getLastMailedCode(), [] (Result<Done>) {});
            wait();
            auth.setNickname ("soak", [] (Result<Done>) {});
            wait();
            auth.setPart ("drums", [] (Result<Done>) {});
            wait();
            auth.finishDiscord();
            auth.finishDone();
        }
        ~Rig() { rt.removeListener (&store); }
        void wait (int ms = 200) { clock.advance (ms); }

        /** 가짜 시계를 minutes 분 돌린다 (1초씩: 연출과 이벤트가 그 사이사이 처리된다) */
        void run (int minutes)
        {
            for (int i = 0; i < minutes * 60; ++i)
                clock.advance (1000);
        }
    };

    void expectListSane (const Rig& r)
    {
        std::set<juce::String> ids;
        for (auto& room : r.store.state().publicRooms)
        {
            expect (room.id.isNotEmpty());
            expect (ids.insert (room.id).second, "공개 방 목록에 같은 방이 두 번: " + room.id);
        }
    }

    void runTest() override
    {
        beginTest ("home visible: hours of scripts (rooms added, joined, left, deleted)");
        for (juce::uint32 seed = 1; seed <= 4; ++seed)
        {
            Rig r (seed);
            r.home.setHomeVisible (true);   // rooms.* 구독: 연출의 방 삭제가 rooms.removed 를 보낸다
            r.wait();
            r.run (6 * 60);                 // 4배속 6시간 = 앱에서 24시간치 연출
            expectListSane (r);
        }

        beginTest ("in my approval room as host: hours of scripts with join requests");
        for (juce::uint32 seed = 11; seed <= 12; ++seed)
        {
            Rig r (seed);
            r.home.setHomeVisible (true);
            r.wait();
            CreateRoom c;
            c.type = "public";
            c.entryMode = juce::String ("approval");
            c.title = "soak room";
            c.capacity = 7;
            r.home.createRoom (c, [] (Result<RoomDetail>) {});
            r.wait();
            expect (r.store.state().inRoom());
            r.run (2 * 60);
            expect (r.store.state().inRoom());
            expectListSane (r);
        }
    }
};

static ScriptSoakTests scriptSoakTests;

} // namespace meon::core
