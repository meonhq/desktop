// AuthController: 시작 → 로그인 / 가입 → 인증 → 닉네임 → 파트 → 디스코드 → 완료 → 홈, 다시 켜면 바로 홈
#include "../core/auth/AuthController.h"
#include "../core/api/fake/FakeMeonServer.h"
#include "MeonFixtures.h"

namespace meon::core
{

class AuthTests : public juce::UnitTest
{
public:
    AuthTests() : juce::UnitTest ("AuthController", "Auth") {}

    struct Rig
    {
        MemoryTokenStore& tokens;
        ManualScheduler clock;
        FakeMeonServer server { clock, [] { FakeMeonServer::Options o; o.latencyMs = 10; o.scripts = false; return o; }() };
        MeonApi api { server, 0 };
        Realtime rt { server, clock, 0 };
        AppStore store;
        AuthController auth { api, rt, store, tokens, clock };
        explicit Rig (MemoryTokenStore& t) : tokens (t)
        {
            server.loadWorld (juce::JSON::parse (juce::String::fromUTF8 (MeonFixtures::world_json, MeonFixtures::world_jsonSize)));
            rt.addListener (&store);
        }
        ~Rig() { rt.removeListener (&store); }
        void wait() { clock.advance (100); }
    };

    void runTest() override
    {
        using S = AuthController::Step;
        MemoryTokenStore tokens;
        juce::var saved;

        beginTest ("signup flow");
        {
            Rig r (tokens);
            r.auth.start();
            r.wait();
            expect (r.auth.step() == S::login);
            r.auth.showSignup();
            bool ok = false;
            r.auth.signup ("me@example.com", "chan_keys", "abcd123!", true, [&] (Result<Done> x) { ok = x.ok(); });
            r.wait();
            expect (ok && r.auth.step() == S::verify);
            expect (tokens.value.isNotEmpty());
            r.auth.verify (r.server.getLastMailedCode(), [&] (Result<Done> x) { ok = x.ok(); });
            r.wait();
            expect (ok && r.auth.step() == S::nickname);
            expect (r.rt.getLink() == Realtime::Link::online);
            r.auth.setNickname (juce::String::fromUTF8 ("승찬"), [&] (Result<Done> x) { ok = x.ok(); });
            r.wait();
            expect (r.auth.step() == S::part);
            r.auth.setPart ("keys", [&] (Result<Done> x) { ok = x.ok(); });
            r.wait();
            expect (r.auth.step() == S::discord);
            r.auth.finishDiscord();
            expect (r.auth.step() == S::done && ! r.auth.cameFromLogin());
            r.auth.finishDone();
            expect (r.auth.step() == S::ready);
            saved = r.server.saveState();
        }

        beginTest ("restart with saved token goes straight home");
        {
            Rig r (tokens);
            r.server.loadState (saved);
            r.auth.start();
            r.wait();
            expect (r.auth.step() == S::ready);
            expect (r.store.state().me.has_value());
        }

        beginTest ("login shows done once; logout back to login");
        {
            tokens.clear();
            Rig r (tokens);
            r.server.loadState (saved);
            r.auth.start();
            r.wait();
            expect (r.auth.step() == S::login);
            bool ok = true;
            r.auth.login ("me@example.com", "wrong123!", [&] (Result<Done> x) { ok = x.ok(); });
            r.wait();
            expect (! ok && r.auth.step() == S::login);
            r.auth.login ("me@example.com", "abcd123!", [&] (Result<Done> x) { ok = x.ok(); });
            r.wait();
            expect (ok && r.auth.step() == S::done && r.auth.cameFromLogin());
            r.auth.logout();
            r.wait();
            expect (r.auth.step() == S::login && tokens.value.isEmpty());
        }
    }
};

static AuthTests authTests;

} // namespace meon::core
