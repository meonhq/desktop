// MEON ui - 내 오디오 · 모니터 카드 묶음. 설명은 AudioDock.h
#include "AudioDock.h"
#include "../dev/RepaintProbe.h"
#include "../../core/model/PanelWidth.h"

namespace meon::ui
{

int AudioDock::devPaintCount = 0;

AudioDock::AudioDock (core::MeonCore& c, core::JamSession& j) : core (c), jam (j)
{
    setInterceptsMouseClicks (true, true);   // 카드 밖은 hitTest 가 걸러 낸다
    addAndMakeVisible (sendCard);
    addAndMakeVisible (monCard);
    sendCard.setTitle (TXT ("내 오디오"), true, true);
    monCard.setTitle (TXT ("모니터"), true, true);
    sendCard.onToggle = [this] (bool o) { sendOpen = o; toggleFold (foldSend, o); };
    monCard.onToggle = [this] (bool o) { monOpen = o; toggleFold (foldMon, o); };
    for (auto* comp : std::initializer_list<juce::Component*> { &sendMute, &sendGain })
        sendCard.addAndMakeVisible (comp);
    for (auto* comp : std::initializer_list<juce::Component*> { &monMute, &master })
        monCard.addAndMakeVisible (comp);
    sendMute.setTooltip (TXT ("다른 사람에게 보내는 내 소리"));
    monMute.setTooltip (TXT ("내가 듣는 모든 소리"));
    sendGain.setTitle (TXT ("송출 레벨 · 더블클릭하면 0 dB"));
    master.setTitle (TXT ("마스터 볼륨 · 더블클릭하면 0 dB"));
    sendMute.onPress = [this] { jam.setSendMuted (! jam.isSendMuted()); refresh(); };
    monMute.onPress = [this] { jam.setMonitorMuted (! jam.isMonitorMuted()); refresh(); };
    sendGain.onChange = [this] (float db) { jam.setSendGainDb (db); };
    master.onChange = [this] (float db) { jam.setMasterDb (db); };
    setMeterSources();
    jam.addListener (this);
    refresh();
}

AudioDock::~AudioDock()
{
    jam.removeListener (this);
}

void AudioDock::setMeterSources()
{
    if (devDrive)
    {
        // 개발용: 소리 없이 미터가 움직이게 (다시 그리기 확인)
        auto wave = [] (int ch) { const double t = juce::Time::getMillisecondCounterHiRes() * 0.001; return (float) (0.35 + 0.3 * std::sin (t * 7.0 + ch * 1.3)); };
        inputMeter.meter.setSource (2, wave);
        outputMeter.meter.setSource (2, wave);
        return;
    }
    // 송출 정지 중에는 보내는 신호가 0 이므로 입력 레벨을 회색으로 보여 준다 (refresh)
    inputMeter.meter.setSource (juce::jlimit (1, 2, jam.sendChannelCount()),
                                [this] (int ch) { return jam.isSendMuted() ? jam.inputLevel (ch) : jam.sendLevel (ch); });
    outputMeter.meter.setSource (2, [this] (int ch) { return jam.monitorLevel (ch); });
}

void AudioDock::devDriveMeters (bool b)
{
    devDrive = b;
    setMeterSources();
}

void AudioDock::OpaqueMeterBox::paint (juce::Graphics& g)
{
    g.fillAll (col::white);   // 카드 면 (모서리 밖까지): 불투명이라 뒤를 그리지 않는다
    MeterBox::paint (g);
}

//==============================================================================
void AudioDock::refresh()
{
    const bool sm = jam.isSendMuted(), mm = jam.isMonitorMuted();
    sendMute.setOn (sm);
    sendMute.setButtonText (sm ? TXT ("송출 재개") : TXT ("송출 정지"));
    sendGain.setMuted (sm);
    inputMeter.meter.setGreyed (sm);
    if (! sendGain.isDragging()) sendGain.setValue (jam.getSendGainDb());
    monMute.setOn (mm);
    monMute.setButtonText (mm ? TXT ("음소거 해제") : TXT ("전체 음소거"));
    master.setMuted (mm);
    if (! master.isDragging()) master.setValue (jam.getMasterDb());
    outputMeter.meter.setActive (! mm);
    resized();   // 버튼 글자 폭이 바뀐다
}

void AudioDock::jamStatsChanged()
{
    // 이 카드 밖에서 바뀌면 (단축키, 플러그인의 DAW 상태, 설정) 0.4초 안에 따라간다
    if (sendMute.isOn() != jam.isSendMuted() || monMute.isOn() != jam.isMonitorMuted()
        || (! sendGain.isDragging() && sendGain.getValue() != jam.getSendGainDb())
        || (! master.isDragging() && master.getValue() != jam.getMasterDb()))
        refresh();
}

//==============================================================================
void AudioDock::setCardsBounds (juce::Rectangle<int> r)
{
    setBounds (r.expanded (PopoverSurface::shadowMargin()));
}

void AudioDock::setShadow (float s)
{
    s = juce::jlimit (0.0f, 1.0f, s);
    if (s == shadow) return;
    shadow = s;
    repaint();
}

bool AudioDock::hitTest (int x, int y)
{
    // 카드 두 장만 (사이 9·그림자 자리는 뒤의 방 목록이 받는다)
    return sendCard.getBounds().contains (x, y) || monCard.getBounds().contains (x, y);
}

void AudioDock::paint (juce::Graphics& g)
{
    ++devPaintCount;
    devprobe::note (*this, g, "AudioDock");
    if (shadow <= 0.0f) return;
    shadows[0].paint (g, *this, sendCard.getBounds(), shadow);
    shadows[1].paint (g, *this, monCard.getBounds(), shadow);
}

void AudioDock::resized()
{
    const auto area = cards();
    const int availW = juce::jmax (0, area.getWidth() - metric::gap);
    bottomAvail = availW;
    const bool hMoving = fold.isMoving (foldSend) || fold.isMoving (foldMon);
    const float eSend = fold.value (foldSend), eMon = fold.value (foldMon);
    // 미터 폭: 열리면 150, 폭이 모자라면 슬라이더가 먼저 60 까지 줄고 그다음 미터가 84 까지 (멤버 행과 같은 규칙,
    // 합주 최종 디자인 6). 접히면 행 전체. bw: 그 줄 왼쪽 버튼 (송출 정지 · 전체 음소거)
    auto meterW = [] (int rowW, bool open, int bw)
    {
        return open ? core::panel::shrink (rowW - bw - 12 - 12, std::numeric_limits<int>::max()).meter : rowW;
    };
    const int sendBw = sendMute.getIdealWidth(), monBw = monMute.getIdealWidth();
    static constexpr int rowInset = 2 + metric::padX * 2;   // 카드 테두리 + 좌우 여백
    core::FoldLayout::Pair ws;
    int sendMeterW, monMeterW;
    if (! hMoving)
    {
        ws = core::FoldLayout::horizontalPair (availW, sendOpen, monOpen);
        sendMeterW = meterW (ws.first - rowInset, sendOpen, sendBw);
        monMeterW = meterW (ws.second - rowInset, monOpen, monBw);
    }
    else
    {
        // 움직이는 동안: 폭·미터 폭을 네 끝 상태에서 이중 선형 보간 (e 가 0·1 이면 기존 값 그대로)
        core::FoldLayout::Pair L[2][2];
        int MS[2][2], MM[2][2];
        for (int a = 0; a < 2; ++a)
            for (int b = 0; b < 2; ++b)
            {
                L[a][b] = core::FoldLayout::horizontalPair (availW, a == 1, b == 1);
                MS[a][b] = meterW (L[a][b].first - rowInset, a == 1, sendBw);
                MM[a][b] = meterW (L[a][b].second - rowInset, b == 1, monBw);
            }
        ws = core::FoldLayout::blend (L, eSend, eMon, foldFillH);
        sendMeterW = core::FoldLayout::blendValue (MS, eSend, eMon);
        monMeterW = core::FoldLayout::blendValue (MM, eSend, eMon);
    }
    const int sendW = ws.first, monW = ws.second;
    auto bottom = area;
    sendCard.setBounds (bottom.removeFromLeft (sendW));
    bottom.removeFromLeft (metric::gap);
    monCard.setBounds (bottom.removeFromLeft (monW));
    sendCard.setOpen (sendOpen);
    sendCard.setFoldProgress (eSend);
    monCard.setOpen (monOpen);
    monCard.setFoldProgress (eMon);

    // row: 카드 좌표. 미터 자리는 이 구성 요소 좌표로 돌려준다 (미터는 형제라서)
    auto layoutRow = [] (juce::Rectangle<int> row, bool open, int mw, Button& b, DbSlider& s)
    {
        row = row.reduced (metric::padX, 12);
        const auto meter = row.removeFromRight (mw).withSizeKeepingCentre (mw, metric::smallH);
        b.setVisible (open);
        s.setVisible (open);
        b.setAlpha (1.0f);
        s.setAlpha (1.0f);
        if (open)
        {
            row.removeFromRight (12);
            const int bw = b.getIdealWidth();
            b.setBounds (row.removeFromLeft (bw).withSizeKeepingCentre (bw, metric::smallH));
            row.removeFromLeft (12);
            s.setBounds (row.withSizeKeepingCentre (row.getWidth(), metric::smallH));
        }
        return meter;
    };
    // 접히는·펼쳐지는 카드: 내용 행을 카드 오른쪽 끝에 붙인다. 미터는 끝까지 보이고(투명도 그대로),
    // 버튼·슬라이더는 시작·끝 중 넓은 쪽의 열린 배치 크기 그대로 미터 왼쪽에 붙어 잘려 들어가며 투명도 e
    auto layoutRowFolding = [&meterW] (juce::Rectangle<int> row, float e, int mw, int contentW, Button& b, DbSlider& s)
    {
        row = row.reduced (metric::padX, 12);
        const auto meter = juce::Rectangle<int> (row.getRight() - mw, row.getY(), mw, row.getHeight()).withSizeKeepingCentre (mw, metric::smallH);
        const int openRowW = contentW - rowInset;
        const int bw = b.getIdealWidth();
        const int sw = juce::jmax (0, openRowW - meterW (openRowW, true, bw) - 12 - bw - 12);
        const int sx = meter.getX() - 12 - sw;
        s.setBounds (sx, meter.getY(), sw, metric::smallH);
        b.setBounds (sx - 12 - bw, meter.getY(), bw, metric::smallH);
        b.setVisible (e > 0.0f);
        s.setVisible (e > 0.0f);
        b.setAlpha (e);
        s.setAlpha (e);
        return meter;
    };
    const auto sendRow = sendCard.contentBounds().removeFromTop (43);
    const auto sendMeter = fold.isMoving (foldSend)
        ? layoutRowFolding (sendRow, eSend, sendMeterW, juce::jmax (foldSendW, sendW), sendMute, sendGain)
        : layoutRow (sendRow, sendOpen, sendMeterW, sendMute, sendGain);
    const auto monRow = monCard.contentBounds().removeFromTop (43);
    const auto monMeter = fold.isMoving (foldMon)
        ? layoutRowFolding (monRow, eMon, monMeterW, juce::jmax (foldMonW, monW), monMute, master)
        : layoutRow (monRow, monOpen, monMeterW, monMute, master);
    // 카드 안 자리를 넘지 않게 (아주 좁은 창)
    meterRects[0] = (sendMeter + sendCard.getPosition()).getIntersection (sendCard.getBounds().reduced (1));
    meterRects[1] = (monMeter + monCard.getPosition()).getIntersection (monCard.getBounds().reduced (1));
    placeMeters();
}

void AudioDock::moved() { placeMeters(); }
void AudioDock::visibilityChanged() { placeMeters(); }

void AudioDock::parentHierarchyChanged()
{
    // 미터 상자는 부모의 자식 (이 구성 요소 바로 위)
    auto* p = getParentComponent();
    for (auto* m : std::initializer_list<juce::Component*> { &inputMeter, &outputMeter })
    {
        if (m->getParentComponent() == p) continue;
        if (auto* old = m->getParentComponent()) old->removeChildComponent (m);
        if (p != nullptr) p->addChildComponent (m, p->getIndexOfChildComponent (this) + 1);
    }
    placeMeters();
}

void AudioDock::raiseMeters()
{
    inputMeter.toFront (false);
    outputMeter.toFront (false);
}

void AudioDock::placeMeters()
{
    const bool show = isVisible() && getParentComponent() != nullptr;
    inputMeter.setBounds (meterRects[0] + getPosition());
    outputMeter.setBounds (meterRects[1] + getPosition());
    inputMeter.setVisible (show && ! meterRects[0].isEmpty());
    outputMeter.setVisible (show && ! meterRects[1].isEmpty());
}

//==============================================================================
void AudioDock::toggleFold (int panel, bool open)
{
    // 움직이는 동안 내용을 배치할 크기 = 지금 크기와 끝 크기 중 큰 쪽 (이미 움직이는 중이면 그때 정한 크기와도)
    juce::ignoreUnused (panel);
    const bool moving = fold.isMoving (foldSend) || fold.isMoving (foldMon);
    const auto end = core::FoldLayout::horizontalPair (bottomAvail, sendOpen, monOpen);
    foldSendW = juce::jmax (moving ? foldSendW : 0, sendCard.getWidth(), end.first);
    foldMonW = juce::jmax (moving ? foldMonW : 0, monCard.getWidth(), end.second);
    foldFillH = sendCard.getWidth() + monCard.getWidth() == bottomAvail && (sendOpen || monOpen);
    fold.setOpen (panel, open);
}

void AudioDock::devToggleFold (const juce::String& panel)
{
    if (panel == "send")     { sendOpen = ! sendOpen; toggleFold (foldSend, sendOpen); }
    else if (panel == "mon") { monOpen = ! monOpen; toggleFold (foldMon, monOpen); }
}

void AudioDock::previewFold (const juce::String& panel, float e)
{
    const int i = panel == "send" ? foldSend : panel == "mon" ? foldMon : -1;
    if (i < 0) return;
    if (i == foldSend) sendOpen = false;
    if (i == foldMon) monOpen = false;
    if (e < 0.0f) { fold.snap (i, false); return; }
    toggleFold (i, false);
    fold.freeze (i, false, e);
}

} // namespace meon::ui
