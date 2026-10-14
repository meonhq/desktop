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

        // refresh 토큰은 한 번 쓰면 바뀐다. 요청 401 의 갱신과 실시간 연결 거절의 갱신이 겹쳐도
        // 한 번만 보내고 결과를 같이 받아야 한다 (예전에는 늦게 간 쪽이 지워진 토큰을 보내 로그아웃됐다)
        beginTest ("overlapping refreshes share one request and stay logged in");
        {
            tokens.clear();
            Rig r (tokens);
            r.server.loadState (saved);
            r.auth.start();
            r.wait();
            bool ok = false;
            r.auth.login ("me@example.com", "abcd123!", [&] (Result<Done> x) { ok = x.ok(); });
            r.wait();
            r.auth.finishDone();
            expect (ok && r.auth.step() == S::ready);

            r.clock.advance (16 * 60 * 1000);   // access 토큰 만료 (15분)
            bool meOk = false, refreshOk = false;
            r.api.getMe ([&] (Result<Me> m) { meOk = m.ok(); });
            r.clock.advance (10);                // getMe 가 401 을 받아 갱신을 보낸 상태
            r.api.refresh ([&] (Result<Tokens> t) { refreshOk = t.ok(); });
            r.wait();
            r.wait();
            expect (meOk, "request retried after 401 succeeds");
            expect (refreshOk, "overlapping refresh succeeds");
            expect (r.auth.step() == S::ready);
            expect (tokens.value.isNotEmpty() && tokens.value == r.api.getTokens().refreshToken);
            saved = r.server.saveState();
        }

        // 서버가 토큰을 회전·저장한 뒤, 앱이 새 토큰을 받기 전에 죽었다가 다시 켜도 로그인이 유지된다
        // (가짜 서버의 재사용 유예. 진짜 서버 반영 대기)
        beginTest ("app dies between server rotation and client save: still logged in after restart");
        {
            const auto before = tokens.value;
            juce::var onDisk = saved;
            {
                Rig r (tokens);
                r.server.loadState (saved);
                r.server.onStateChanged = [&] (const juce::var& v) { onDisk = v; };   // MEONFakeServer.json 쓰기
                r.auth.start();
                r.clock.advance (11);   // 설정 받음 → refresh 를 보냄: 서버는 회전·저장, 응답은 아직
                expect (tokens.value == before);
            }                           // 여기서 앱이 죽는다
            Rig r2 (tokens);
            r2.server.loadState (onDisk);
            r2.auth.start();
            r2.wait();
            expect (r2.auth.step() == S::ready);
            expect (tokens.value.isNotEmpty() && tokens.value != before);
        }

        beginTest ("rejected saved token: login screen, stored token cleared");
        {
            tokens.value = "rt_not_valid";
            Rig r (tokens);
            r.server.loadState (saved);
            r.auth.start();
            r.wait();
            expect (r.auth.step() == S::login);
            expect (tokens.value.isEmpty());
        }
    }
};

static AuthTests authTests;

} // namespace meon::core
