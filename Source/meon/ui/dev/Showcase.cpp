#include "Showcase.h"
#include "ShowcaseEngine.h"
#include "../AppRoot.h"
#include "../screens/HomeScreen.h"
#include "../screens/JamScreen.h"
#include "../widgets/Button.h"
#include "../widgets/Display.h"
#include "../overlays/SettingsModal.h"
#include "../widgets/Meter.h"
#include "../../core/log/AppLog.h"
#include "../../shell/ShellHooks.h"

#if ! JUCE_MSVC
 #include <cxxabi.h>
#endif

namespace meon::ui::dev
{

using juce::String;

namespace
{
    // 첫 화면 영상에서 새로 올라오는 메시지 (4초: 상대, 8초: 나)
    const char* const videoLine4 = "베이스 잘 들려요? 조금 올릴까요?";
    const char* const videoLine8 = "좋아요. 브릿지만 한 번 더 가요";

    // 입장 요청을 보내는 사람 (request · joined 장면). showcase.json 의 join_requesters
    const Showcase::CastMember joiner { "u_s_jiwoo", "지우", "bass", -2.0f, 20, 3, 0.0, 0.0, 3 };

    // 파일 플레이어 (file 장면): 위치는 고정 (재생 중 모양만)
    const char* const fileName = "반주 - 재즈 스윙 120.wav";
}

const std::vector<Showcase::CastMember>& Showcase::cast()
{
    // 핑은 거의 고정, 12초 주기 안에서 한 번씩만 1 ms 바뀐다. 모든 값이 2~4 ms (5 ms 이상은 어디에도 나오지 않게).
    // 볼륨·패닝은 사람마다 조금씩 다르게
    static const std::vector<CastMember> c {
        { "u_s_minjun",  "민준", "guitar", -2.0f, -35, 3, 3400.0,  6100.0, 4 },
        { "u_s_seoyeon", "서연", "vocal",   1.0f,  10, 2, 9900.0, 10900.0, 3 },
        { "u_s_doyun",   "도윤", "drums",  -4.0f,   0, 3, 7000.0,  9100.0, 2 },
        { "u_s_haeun",   "하은", "bass",   -1.0f, -15, 4,    0.0,     0.0, 4 },
        { "u_s_jiho",    "지호", "guitar", -3.0f,  40, 4, 1200.0,  2900.0, 3 },
        { "u_s_yuna",    "유나", "vocal",   0.0f, -10, 2, 5200.0,  6400.0, 3 },
    };
    return c;
}

Showcase::Showcase (core::MeonCore& c, AppRoot& r, juce::Component& t, const juce::StringPairArray& dev)
    : core (c), root (r), target (t)
{
    scene = dev["showcase"].isNotEmpty() ? dev["showcase"] : String ("jam");
    mode = dev["capture"].isNotEmpty() ? dev["capture"] : String ("still");
    outDir = juce::File::getCurrentWorkingDirectory().getChildFile (dev["capture-out"].isNotEmpty() ? dev["capture-out"] : String ("showcase-out"));
    if (dev.containsKey ("capture-at"))     stillAtMs = dev["capture-at"].getDoubleValue();
    if (dev.containsKey ("capture-frames")) frames = juce::jmax (1, dev["capture-frames"].getIntValue());
    if (dev.containsKey ("capture-scale"))  scale = juce::jlimit (1.0f, 4.0f, dev["capture-scale"].getFloatValue());
    if (dev.containsKey ("capture-wait"))   waitMs = juce::jmax (0, dev["capture-wait"].getIntValue());
    captureName = dev["capture-name"];
    chatCase = dev["chat-case"];
    if (dev.containsKey ("capture-size"))
        size = { dev["capture-size"].upToFirstOccurrenceOf ("x", false, false).getIntValue(),
                 dev["capture-size"].fromFirstOccurrenceOf ("x", false, false).getIntValue() };
}

Showcase::~Showcase()
{
    *alive = false;
    stopTimer();
    ShowcaseEngine::clockMs() = -1.0;
}

void Showcase::add (int delayMs, String what, std::function<void()> run, std::function<bool()> until, int timeoutMs)
{
    steps.push_back ({ delayMs, std::move (run), std::move (until), timeoutMs, std::move (what) });
}

void Showcase::fail (const String& why)
{
    stopTimer();
    std::cerr << "SHOWCASE FAIL " << why.toStdString() << std::endl;
    if (onDone) onDone (1);
}

void Showcase::start()
{
    // 홈 '내 오디오' 의 장치 이름을 비운다 (CI 러너의 가상 장치 이름이 화면에 나오지 않게. 장치가 없을 때와 같은 표시)
    meon::deviceManagerProvider() = [] { return (juce::AudioDeviceManager*) nullptr; };
    outDir.createDirectory();
    std::cerr << "SHOWCASE start scene=" << scene.toStdString() << " mode=" << mode.toStdString() << " out=" << outDir.getFullPathName().toStdString() << std::endl;
    if (scene == "jam") buildJam();
    else if (scene == "request" || scene == "joined") { buildJam ("u_s_haeun"); buildRequest (scene == "joined"); }
    else if (scene == "file" || scene == "file-empty") { buildJam(); buildFile (scene == "file"); }
    else if (scene == "rooms") buildHome (false);
    else if (scene == "rooms-before") buildHome (false, false);
    else if (scene == "friends") buildHome (true);
    else if (scene == "earguard") { buildHome (false, false); buildEarGuard(); }
    else if (scene == "chat") buildChat();
    else { fail ("unknown scene " + scene); return; }
    addCapture();
    startTimer (steps.empty() ? 1 : juce::jmax (1, steps.front().delayMs));
}

void Showcase::timerCallback()
{
    if (next >= steps.size()) { stopTimer(); return; }
    auto& s = steps[next];
    if (! waiting)
    {
        if (s.run) s.run();
        waiting = true;
        waitStarted = juce::Time::currentTimeMillis();
    }
    if (s.until && ! s.until())
    {
        if (juce::Time::currentTimeMillis() - waitStarted > s.timeoutMs) { fail ("timeout: " + s.what); return; }
        startTimer (50);
        return;
    }
    std::cerr << "SHOWCASE step " << s.what.toStdString() << std::endl;
    waiting = false;
    if (++next >= steps.size()) { stopTimer(); return; }
    startTimer (juce::jmax (1, steps[next].delayMs));
}

//==============================================================================
// 장면

void Showcase::buildJam (const String& skip)
{
    auto* jam = core.jam();
    if (jam == nullptr) { fail ("no jam session"); return; }

    add (300, "logged in", {}, [this] { return core.store().state().me.has_value() && root.getHome() != nullptr; });
    add (300, "create room", [this]
    {
        core::CreateRoom c;
        c.type = "public";
        c.entryMode = String ("approval");
        c.title = String::fromUTF8 ("금요일 밤 잼");
        c.description = String::fromUTF8 ("시티팝, 펑크 위주로 셋리스트 돌려요.");
        c.capacity = 7;
        core.home().createRoom (c, [] (core::Result<core::RoomDetail>) {});
    }, [jam] { return jam->isInRoom(); });

    add (200, "self monitor", [jam]
    {
        jam->setSelfMonitor (true);
        jam->setSelfMonitorDb (-3.0f);
    });

    auto joinMember = [this, jam] (const CastMember& m)
    {
        ShowcaseEngine::setPart (m.id, m.part);
        ShowcaseEngine::setPing (m.id, m.pingMs, m.bumpFrom, m.bumpTo, m.bumpMs);
        if (auto* fake = core.fakeServer())
            fake->remotePeerJoined (m.id, String::fromUTF8 (m.nickname), m.part, false);
        juce::ignoreUnused (jam);
    };
    auto connected = [jam] (const char* id)
    {
        for (auto& m : jam->members())
            if (m.userId == id) return m.state == core::PeerState::State::connected;
        return false;
    };
    auto say = [jam] (const char* from, const char* text)
    {
        static int n = 0;
        auto& chat = jam->chat();
        chat.received ({ chat.group(), from, "id:sc" + String (++n), String::fromUTF8 (text) });
    };
    auto mine = [jam] (const char* text) { jam->chat().sendText (String::fromUTF8 (text)); };

    // 들어오는 순서와 대화: 입장 안내 줄은 앞쪽에 몰려 위로 밀려 올라가고, 화면에는 마지막 입장(유나)부터 보인다
    const auto& c = cast();
    auto join = [this, joinMember, connected, skip] (const CastMember& m)
    {
        if (skip == m.id) return;
        add (500, String ("join ") + m.id, [joinMember, &m] { joinMember (m); }, [connected, id = m.id] { return connected (id); });
    };
    join (c[0]); join (c[1]); join (c[2]);
    add (500, "chat 1", [say] { say ("u_s_minjun", "셋리스트 올려 뒀어요. 오늘은 네 곡 가요"); });
    add (400, "chat 2", [say] { say ("u_s_seoyeon", "첫 곡은 뭐부터 해요?"); });
    add (400, "chat 3", [mine] { mine ("셋리스트 1번부터 순서대로 가요"); });
    join (c[3]); join (c[4]);
    add (500, "chat 4", [say] { say ("u_s_doyun", "드럼 소리 크면 바로 말해 주세요"); });
    join (c[5]);
    add (600, "chat 5", [say] { say ("u_s_yuna", "늦어서 죄송해요! 바로 준비할게요"); });
    add (400, "chat 6", [mine] { mine ("괜찮아요. 첫 곡은 템포 120으로\n갈게요"); });
    add (400, "chat 7", [say] { say ("u_s_seoyeon", "키 하나 올려도 될까요?"); });
    add (400, "chat 8", [mine] { mine ("좋아요, 그렇게 가요"); });

    add (300, "mix", [jam, skip]
    {
        for (auto& m : cast())
        {
            if (skip == m.id) continue;
            jam->setMemberGainDb (m.id, m.gainDb);
            jam->setMemberPan (m.id, m.pan);
        }
    });
}

void Showcase::buildHome (bool friendsScene, bool sendRequest)
{
    add (300, "logged in", {}, [this]
    {
        const auto& s = core.store().state();
        return s.me.has_value() && root.getHome() != nullptr && ! s.publicRooms.empty() && ! s.friends.empty();
    });
    // 내 RTT 2 ms: 방장 RTT(1·2·4 ms, showcase.json)와 계수 0.7 로 예상 지연이 2~4 ms
    add (200, "my rtt", [this] { core.store().setMyRtt (2); });
    if (! sendRequest)
    {
        if (friendsScene)
            add (300, "invite", [this] { if (auto* fake = core.fakeServer()) fake->devInviteMe(); },
                 [this] { return ! core.store().state().invites.empty(); });
        return;
    }
    add (300, "request", [this]
    {
        core.home().joinRoom ("r_s_rock", [] (core::Result<core::JoinResult>) {});
    }, [this]
    {
        for (auto& r : core.store().state().publicRooms)
            if (r.id == "r_s_rock") return core.home().isRequestPending (r);
        return false;
    });
    if (friendsScene)
        add (300, "invite", [this] { if (auto* fake = core.fakeServer()) fake->devInviteMe(); },
             [this] { return ! core.store().state().invites.empty(); });
}

void Showcase::buildRequest (bool accept)
{
    // 방장 시점: 지우(베이스)가 입장 요청을 보낸다. 정원은 7명까지라 하은을 뺀 6명 방에서 받는다 (수락할 수 있게).
    // request = 입장 요청 목록을 연 채로, joined = 수락해서 지우가 멤버 목록 맨 아래에 들어온 뒤
    auto* jam = core.jam();
    if (jam == nullptr) return;
    add (300, "join request", [this]
    {
        ShowcaseEngine::setPart (joiner.id, joiner.part);
        ShowcaseEngine::setPing (joiner.id, joiner.pingMs, joiner.bumpFrom, joiner.bumpTo, joiner.bumpMs);
        if (auto* fake = core.fakeServer()) fake->devJoinRequest();
    }, [this] { return core.store().state().joinRequests.size() == 1; });
    if (! accept)
    {
        add (300, "open requests", [this]
        {
            if (auto* b = findButton (target, String::fromUTF8 ("입장 요청"))) b->triggerClick();
            else fail ("no request button");
        }, [this] { return findType (target, "Popover") != nullptr; });
        return;
    }
    add (300, "accept", [this]
    {
        core.room().acceptJoinRequest (core.store().state().joinRequests.front().id);
    }, [jam]
    {
        for (auto& m : jam->members())
            if (m.userId == joiner.id) return m.state == core::PeerState::State::connected;
        return false;
    });
    add (300, "mix", [jam] { jam->setMemberGainDb (joiner.id, joiner.gainDb); jam->setMemberPan (joiner.id, joiner.pan); });
}

void Showcase::buildFile (bool load)
{
    // 오른쪽 패널을 파일 이름이 다 보이는 폭으로 넓힌다 (사용자가 경계를 끌어 넓히는 것과 같다. 저장하지 않음).
    // file-empty 와 file 이 같은 폭이 되게 "파일 변경" 버튼 폭으로 잰다 (FilePlayer::paint 의 배치와 같은 계산)
    auto* jam = core.jam();
    if (jam == nullptr) return;
    add (300, "side width", [this]
    {
        auto* js = root.getJamScreen();
        if (js == nullptr) { fail ("no jam screen"); return; }
        Button change { Button::Kind::small, String::fromUTF8 ("파일 변경") };
        const int nameW = (int) std::ceil (textWidth (font (600, 14.0f), String::fromUTF8 (fileName)));
        const int timeW = (int) std::ceil (textWidth (font (400, 12.0f), "00:00 / 00:00"));
        const int need = 18 + nameW + 9 + timeW + 9 + change.getIdealWidth() + 18 + 4;
        const int w = (need + 9) / 10 * 10;
        js->setSideWidth (juce::jmax (js->sidePanelWidth(), w), false);
        std::cerr << "SHOWCASE side width " << js->sidePanelWidth() << " (need " << need << ")" << std::endl;
    });
    if (! load) return;
    add (300, "file", [jam]
    {
        jam->loadFile (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (String::fromUTF8 (fileName)));
        jam->setFilePlaying (true);
    }, [jam] { return jam->hasFile() && jam->isFilePlaying(); });
}

void Showcase::buildEarGuard()
{
    // 설정 › 합주 탭의 "귀 테러 방지" (보통 = 켜짐)
    add (300, "ear guard", [this]
    {
        if (auto* jam = core.jam()) jam->setEarGuardLevel (1);
        if (root.onOpenSettings) root.onOpenSettings ((int) SettingsModal::Tab::jam);
    }, [this] { return findType (target, "SettingsModal") != nullptr; });
}

juce::Button* Showcase::findButton (juce::Component& c, const String& tooltip)
{
    if (auto* b = dynamic_cast<juce::Button*> (&c))
        if (c.isShowing() && b->getTooltip() == tooltip) return b;
    for (auto* child : c.getChildren())
        if (auto* b = findButton (*child, tooltip)) return b;
    return nullptr;
}

juce::Component* Showcase::findType (juce::Component& c, const String& typeSuffix)
{
    if (c.isVisible() && String (typeid (c).name()).contains (typeSuffix)) return &c;
    for (auto* child : c.getChildren())
        if (auto* f = findType (*child, typeSuffix)) return f;
    return nullptr;
}

void Showcase::buildChat()
{
    // 채팅 말풍선 줄바꿈 확인: 서연(상대) 한 줄, 나 한 줄. 띄어쓰기 자리에서 줄이 바뀌고, 한 줄보다 긴 단어만 글자 단위로 끊는지 본다
    struct Case { const char* id; const char* other; const char* mine; };
    static const Case cases[] = {
        { "ko",      "오늘 합주 끝나고 다음 주 셋리스트도 같이 정해 볼까요? 저는 시티팝 한 곡 더 넣고 싶어요",
                     "좋아요. 첫 곡은 템포 120으로 갈게요. 키는 원곡보다 하나 올려서 해 봐요" },
        { "en",      "Let's run the bridge one more time and then take a short break before the last song",
                     "Sounds good. I will count in four and we start again from the second verse" },
        { "link",    "악보 여기 올렸어요 https://drive.example.com/folder/1A2b3C4d5E6f7G8h9I0jKlMnOpQr/setlist-final.pdf",
                     "녹음은 https://music.example.com/playlist/37i9dQZF1DXcBWIGoYBM5M?si=a1b2c3d4 에 있어요" },
        { "nospace", "띄어쓰기없이아주길게쓴문장도말풍선안에서글자단위로자연스럽게끊어져야해요",
                     "합주끝나고녹음파일정리해서단톡방에올려둘게요다들확인해주세요" },
    };
    const Case* c = &cases[0];
    for (auto& k : cases)
        if (chatCase == k.id) c = &k;

    auto* jam = core.jam();
    if (jam == nullptr) { fail ("no jam session"); return; }
    add (300, "logged in", {}, [this] { return core.store().state().me.has_value() && root.getHome() != nullptr; });
    add (300, "create room", [this]
    {
        core::CreateRoom r;
        r.type = "public";
        r.entryMode = String ("approval");
        r.title = String::fromUTF8 ("금요일 밤 잼");
        r.capacity = 7;
        core.home().createRoom (r, [] (core::Result<core::RoomDetail>) {});
    }, [jam] { return jam->isInRoom(); });
    const auto& m = cast()[1];   // 서연
    add (300, "join", [this, &m]
    {
        ShowcaseEngine::setPart (m.id, m.part);
        ShowcaseEngine::setPing (m.id, m.pingMs, 0.0, 0.0, m.pingMs);
        if (auto* fake = core.fakeServer())
            fake->remotePeerJoined (m.id, String::fromUTF8 (m.nickname), m.part, false);
    }, [jam, &m]
    {
        for (auto& p : jam->members())
            if (p.userId == m.id) return p.state == core::PeerState::State::connected;
        return false;
    });
    add (400, "other", [jam, c, &m]
    {
        auto& chat = jam->chat();
        chat.received ({ chat.group(), m.id, "id:wrap1", String::fromUTF8 (c->other) });
    });
    add (300, "mine", [jam, c] { jam->chat().sendText (String::fromUTF8 (c->mine)); });
}

void Showcase::addCapture()
{
    add (2500, "settle", {});
    add (0, "detach", [this]
    {
        detachFromDesktop();
        hideResizeCorner();
        if (size.x > 0 && size.y > 0) target.setSize (size.x, size.y);
        collectMeters (target);
        baseMs = juce::Time::getMillisecondCounterHiRes() + 100000.0;   // 화면에서 떼기 전 미터가 본 시각보다 뒤
        preroll (mode == "video" ? 0 : stillFrame());
    });
    if (mode == "video")
    {
        add (0, "video", [this]
        {
            (outDir.getChildFile ("frames")).createDirectory();
            frame = 0;
            videoFrame();
        });
        return;
    }
    add (juce::jmax (waitMs, 450), "still", [this]
    {
        stepMeters (stillFrame());
        const auto file = outDir.getChildFile (baseName() + ".png");
        const bool ok = render (file);
        writeLayout();
        std::cerr << "SHOWCASE still " << file.getFullPathName().toStdString() << (ok ? " ok" : " FAILED") << std::endl;
        juce::MessageManager::callAsync ([this, ok, weak = std::weak_ptr<bool> (alive)]
        {
            if (! weak.expired() && onDone) onDone (ok ? 0 : 1);
        });
    });
}

//==============================================================================
// 렌더링

String Showcase::baseName() const
{
    if (captureName.isNotEmpty()) return captureName;
    return scene + (core.isPlugin() ? "-plugin" : "");
}

void Showcase::detachFromDesktop()
{
    // 화면에서 떼면 미터가 화면 주사율(VBlank)로 실제 시간을 따라 움직이지 않는다. 그 뒤로는 캡처 시계로만 움직인다
    if (auto* top = target.getTopLevelComponent())
        if (top->isOnDesktop()) top->removeFromDesktop();
}

void Showcase::hideResizeCorner()
{
    // 플러그인 창 오른쪽 아래 크기 조절 손잡이 (AudioProcessorEditor::setResizable 이 붙인다).
    // 소개 이미지에서는 둥근 창 모서리에 걸려 잘리므로 캡처할 때만 숨긴다. 앱 동작은 그대로다
    for (auto* child : target.getChildren())
        if (dynamic_cast<juce::ResizableCornerComponent*> (child) != nullptr)
            child->setVisible (false);
}

void Showcase::collectPings (juce::Component& c, juce::Array<juce::var>& out)
{
    if (! c.isVisible()) return;
    if (auto* p = dynamic_cast<PingBadge*> (&c)) out.add (p->getText());
    for (auto* child : c.getChildren()) collectPings (*child, out);
}

void Showcase::collectMeters (juce::Component& c)
{
    if (auto* m = dynamic_cast<Meter*> (&c)) meters.emplace_back (m);
    for (auto* child : c.getChildren()) collectMeters (*child);
}

int Showcase::stillFrame() const
{
    return juce::roundToInt (stillAtMs * fps / 1000.0);
}

void Showcase::setClock (int frameIndex)
{
    // 엔진 시계는 반복 주기 안의 위치 (프레임 번호를 정수로 접어서 k 와 k+360 이 정확히 같은 값). 음수는 '실제 시간' 표시라 쓰지 않는다
    const int k = ((frameIndex % loopFrames) + loopFrames) % loopFrames;
    ShowcaseEngine::clockMs() = (double) k * 1000.0 / fps;
}

void Showcase::stepMeters (int frameIndex)
{
    setClock (frameIndex);
    const double now = baseMs + (double) frameIndex * 1000.0 / fps;   // 미터의 시간은 계속 늘어난다
    for (auto& m : meters)
        if (auto* meter = dynamic_cast<Meter*> (m.getComponent())) meter->step (now);
}

void Showcase::preroll (int untilFrame)
{
    // 한 주기(12초) 동안 미리 돌려 미터의 감쇠·피크 유지 상태를 반복 주기에 맞춘다 (첫 프레임 = 마지막 다음 프레임)
    for (int k = untilFrame - loopFrames; k < untilFrame; ++k)
        stepMeters (k);
}

bool Showcase::render (const juce::File& file)
{
    auto image = target.createComponentSnapshot (target.getLocalBounds(), true, scale);
    if (! image.isValid()) return false;
    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk()) return false;
    juce::PNGImageFormat png;
    return png.writeImageToStream (image, out);
}

namespace
{
    String typeName (const juce::Component& c)
    {
        const char* raw = typeid (c).name();
       #if ! JUCE_MSVC
        int status = 0;
        if (char* d = abi::__cxa_demangle (raw, nullptr, nullptr, &status))
        {
            String s (d);
            std::free (d);
            return s;
        }
       #endif
        return String (raw).replace ("struct ", "").replace ("class ", "");
    }

    void dump (const juce::Component& c, const juce::Component& top, int parent, juce::Array<juce::var>& out)
    {
        const auto r = top.getLocalArea (&c, c.getLocalBounds());
        auto* o = new juce::DynamicObject();
        o->setProperty ("type", typeName (c));
        o->setProperty ("parent", parent);
        o->setProperty ("x", r.getX());
        o->setProperty ("y", r.getY());
        o->setProperty ("w", r.getWidth());
        o->setProperty ("h", r.getHeight());
        o->setProperty ("visible", c.isVisible());
        if (auto* b = dynamic_cast<const juce::Button*> (&c)) o->setProperty ("text", b->getButtonText());
        if (auto* p = dynamic_cast<const PingBadge*> (&c)) o->setProperty ("text", p->getText());
        if (auto* t = dynamic_cast<const juce::SettableTooltipClient*> (&c))
            if (auto tip = const_cast<juce::SettableTooltipClient*> (t)->getTooltip(); tip.isNotEmpty()) o->setProperty ("tooltip", tip);
        const int me = out.size();
        out.add (juce::var (o));
        if (! c.isVisible()) return;
        for (auto* child : c.getChildren()) dump (*child, top, me, out);
    }
}

void Showcase::writeLayout()
{
    juce::Array<juce::var> nodes;
    dump (target, target, -1, nodes);
    auto* o = new juce::DynamicObject();
    o->setProperty ("scene", scene);
    o->setProperty ("plugin", core.isPlugin());
    o->setProperty ("width", target.getWidth());
    o->setProperty ("height", target.getHeight());
    o->setProperty ("scale", scale);
    o->setProperty ("nodes", nodes);
    outDir.getChildFile (baseName() + ".layout.json").replaceWithText (juce::JSON::toString (juce::var (o)));
}

void Showcase::videoEvents (double t)
{
    // 4초: 상대 메시지, 8초: 내 메시지 (반복 이음새의 채팅은 후처리에서 마지막 0.5초 동안 첫 장면으로 페이드)
    auto* jam = core.jam();
    if (jam == nullptr) return;
    if (t >= 4000.0 && ! chat4)
    {
        chat4 = true;
        auto& chat = jam->chat();
        chat.received ({ chat.group(), "u_s_haeun", "id:sc-v4", String::fromUTF8 (videoLine4) });
    }
    if (t >= 8000.0 && ! chat8)
    {
        chat8 = true;
        jam->chat().sendText (String::fromUTF8 (videoLine8));
    }
}

void Showcase::videoFrame()
{
    if (frame >= frames)
    {
        writeLayout();
        // 프레임마다 화면의 핑 글자 (후처리에서 2~4 ms 인지 검사)
        outDir.getChildFile ("frames").getChildFile ("pings.json").replaceWithText (juce::JSON::toString (juce::var (framePings)));
        std::cerr << "SHOWCASE video done " << frames << " frames" << std::endl;
        if (onDone) onDone (0);
        return;
    }
    if (frame == 0) writeLayout();   // 첫 프레임의 배치 (채팅 칸 위치는 끝까지 같다)
    const double t = (double) frame * 1000.0 / fps;
    setClock (frame);   // 핑: 0.4초마다 오는 갱신이 이 시각의 값을 읽는다
    videoEvents (t);
    std::weak_ptr<bool> weak = alive;
    juce::Timer::callAfterDelay (waitMs, [this, weak]
    {
        if (weak.expired()) return;
        stepMeters (frame);
        const auto file = outDir.getChildFile ("frames").getChildFile (String (frame).paddedLeft ('0', 4) + ".png");
        if (! render (file)) { fail ("render " + file.getFullPathName()); return; }
        juce::Array<juce::var> texts;
        collectPings (target, texts);
        framePings.add (texts);
        if (frame % 30 == 0) std::cerr << "SHOWCASE frame " << frame << std::endl;
        ++frame;
        juce::MessageManager::callAsync ([this, weak] { if (! weak.expired()) videoFrame(); });
    });
}

} // namespace meon::ui::dev
