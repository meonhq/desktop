#include "SettingsScreen.h"

namespace meon
{

//==============================================================================
SettingsScreen::LinkLabel::LinkLabel (const juce::String& t, const juce::String& u, float p)
    : juce::Button (t), text (t), url (u), px (p)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (false);
    onClick = [this] { juce::URL (url).launchInDefaultBrowser(); };
}

int SettingsScreen::LinkLabel::getIdealWidth() const
{
    return (int) std::ceil (Fonts::get (400, px).getStringWidthFloat (text)) + 2;
}

void SettingsScreen::LinkLabel::paintButton (juce::Graphics& g, bool over, bool)
{
    auto font = Fonts::get (400, px);
    g.setFont (font);
    g.setColour (over ? col::ink : col::inkSub);
    g.drawText (text, getLocalBounds(), juce::Justification::centredLeft, false);
    if (over)
        g.fillRect (0, getHeight() - 2, getIdealWidth() - 2, 1);
}

//==============================================================================
SettingsScreen::Content::Content (SettingsScreen& o)
    : owner (o), plugin (o.plugin),
      nickInput (o.plugin ? 14.0f : 15.0f),
      partSelector (o.plugin ? PartSelector::Style { 6, 22.0f, 8, 13.0f, 500 }
                             : PartSelector::Style { 8, 28.0f, 10, 14.0f, 500 }),
      earGuardTabs ({ TXT ("끔"), TXT ("보통"), TXT ("강하게") }, o.plugin ? 13.0f : 14.0f),
      driverPanelButton (TXT ("드라이버 설정")),
      saveButton (TXT ("저장")), logButton (TXT ("로그 폴더 열기")), updateButton (TXT ("다시 확인")),
      licenseLink (o.plugin ? TXT ("전문 보기") : TXT ("라이선스 전문 보기"), "https://www.gnu.org/licenses/gpl-3.0.html", o.plugin ? 13.0f : 14.0f),
      sourceLink (TXT ("소스 코드"), "https://github.com/unzzonzz/meon", 14.0f),
      showUpdate (! o.plugin && MeonUpdater::isSupported())
{
    meter.setCornerRadius (3.0f);
    nickInput.setIndents (12, 0);
    nickInput.setInputRestrictions (12);
    nickInput.setText (owner.editor.getSettings().getNickname(), juce::dontSendNotification);
    nickInput.onReturnKey = [this] { saveNickname(); };
    saveButton.setFont (plugin ? 13.0f : 14.0f, 500);
    saveButton.onClick = [this] { saveNickname(); };
    // 파트는 누르는 즉시 저장. 방 밖이면 바로 다시 연결해 이름에 반영, 합주 중이면 다음 입장부터
    partSelector.setSelected (parts::fromKey (owner.editor.getSettings().getPartKey()));
    partSelector.onChange = [this] (Part p)
    {
        owner.editor.getSettings().setPartKey (parts::key (p));
        owner.editor.getSession().applyNicknameChange();
    };
    // 귀 테러 방지: 누르는 즉시 저장하고 내 모니터 출력에 적용 (전환은 오디오 쪽에서 부드럽게)
    earGuardTabs.setSelected ((int) owner.editor.getSession().getEarGuardLevel());
    earGuardTabs.onChange = [this] (int i)
    {
        owner.editor.getSession().setEarGuardLevel ((EarGuard::Level) i);
        repaint (earGuardDescArea);
    };
    addAndMakeVisible (earGuardTabs);
    logButton.setFont (plugin ? 13.0f : 14.0f, 500);
    logButton.onClick = []
    {
        auto folder = MeonSettings::getLogFolder();
        folder.createDirectory();
        folder.revealToUser();
    };

    if (! plugin)
    {
        if (auto* dm = owner.editor.deviceManager())
            driverChoices = audio::driverTypes (*dm);
        if (! driverChoices.isEmpty())
        {
            addAndMakeVisible (driverCombo);
            driverCombo.onChange = [this] { applyDriver(); };
        }
        addAndMakeVisible (inCombo);
        addAndMakeVisible (outCombo);
        addAndMakeVisible (bufCombo);
        addChildComponent (driverPanelButton);
        addAndMakeVisible (meter);
        driverPanelButton.setFont (14.0f, 500);
        driverPanelButton.onClick = [this]
        {
            if (auto* dm = owner.editor.deviceManager())
                audio::showDriverPanel (*dm);
            refreshDevices();
        };
        inCombo.onChange = [this] { applyDevices(); };
        outCombo.onChange = [this] { applyDevices(); };
        bufCombo.onChange = [this] { applyBuffer(); };
    }
    addAndMakeVisible (nickInput);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (partSelector);
    addAndMakeVisible (logButton);
    if (showUpdate)
    {
        updateButton.setFont (14.0f, 500);
        updateButton.onClick = [this]
        {
            auto& updater = owner.editor.getUpdater();
            switch (updater.getState())
            {
                case MeonUpdater::State::Available: owner.editor.startUpdate(); break;
                case MeonUpdater::State::Ready:     owner.editor.installUpdate(); break;
                default:                            updater.check(); break;
            }
        };
        addAndMakeVisible (updateButton);
        inRoom = owner.editor.getSession().isInRoom();
        refreshUpdate();
    }
    addAndMakeVisible (licenseLink);
    if (! plugin)
        addAndMakeVisible (sourceLink);
    refreshDevices();
}

void SettingsScreen::Content::refreshDevices()
{
    if (plugin)
        return;
    auto* dm = owner.editor.deviceManager();
    if (dm == nullptr)
        return;
    updating = true;
    auto* type = dm->getCurrentDeviceTypeObject();
    auto setup = dm->getAudioDeviceSetup();
    const juce::String none = TXT ("장치를 찾을 수 없음");

    auto fill = [&none] (juce::ComboBox& box, const juce::StringArray& names, const juce::String& current)
    {
        box.clear (juce::dontSendNotification);
        box.setEnabled (! names.isEmpty());
        box.setTextWhenNothingSelected (none);
        for (int i = 0; i < names.size(); ++i)
            box.addItem (names[i], i + 1);
        const int idx = names.indexOf (current);
        if (idx >= 0)
            box.setSelectedId (idx + 1, juce::dontSendNotification);
    };
    fill (inCombo, type != nullptr ? type->getDeviceNames (true) : juce::StringArray(), setup.inputDeviceName);
    fill (outCombo, type != nullptr ? type->getDeviceNames (false) : juce::StringArray(), setup.outputDeviceName);

    // 버퍼 크기는 첫 실행 화면과 같은 네 가지 (지금 장치가 실제로 지원하는 크기 중 64/128/256/512 에 가장 가까운 것)
    bufCombo.clear (juce::dontSendNotification);
    bufferChoices = audio::bufferChoices (*dm);
    const double sr = setup.sampleRate > 0.0 ? setup.sampleRate : 48000.0;
    const int current = audio::currentBufferSize (*dm);
    auto label = [sr] (int size) { return juce::String (size) + TXT (" 샘플 · ") + juce::String (size / sr * 1000.0, 1) + " ms"; };
    for (int i = 0; i < bufferChoices.size(); ++i)
        bufCombo.addItem (label (bufferChoices[i]), i + 1);
    const int bi = bufferChoices.indexOf (current);
    if (bi >= 0)
        bufCombo.setSelectedId (bi + 1, juce::dontSendNotification);
    else if (current > 0)
        bufCombo.setText (label (current), juce::dontSendNotification);   // 네 가지 밖의 값(장치가 고른 값)은 그대로 보여 준다
    bufCombo.setEnabled (dm->getCurrentAudioDevice() != nullptr && bufferChoices.size() > 1);

    if (! driverChoices.isEmpty())
    {
        driverCombo.clear (juce::dontSendNotification);
        for (int i = 0; i < driverChoices.size(); ++i)
            driverCombo.addItem (audio::driverLabel (driverChoices[i]), i + 1);
        const int di = driverChoices.indexOf (dm->getCurrentAudioDeviceType());
        if (di >= 0)
            driverCombo.setSelectedId (di + 1, juce::dontSendNotification);
    }
    const bool panel = audio::hasDriverPanel (*dm);
    if (panel != driverPanelButton.isVisible())
    {
        driverPanelButton.setVisible (panel);
        if (getParentComponent() != nullptr)   // 생성 중이 아닐 때만 (버퍼 줄 폭이 바뀐다)
            owner.resized();
    }
    updating = false;
}

void SettingsScreen::Content::applyDriver()
{
    if (updating || plugin)
        return;
    auto* dm = owner.editor.deviceManager();
    const int idx = driverCombo.getSelectedId() - 1;
    if (dm == nullptr || idx < 0 || idx >= driverChoices.size())
        return;
    audio::setDriverType (*dm, driverChoices[idx]);
    if (owner.editor.saveSettingsIfNeeded)
        owner.editor.saveSettingsIfNeeded();
    refreshDevices();
}

void SettingsScreen::Content::applyDevices()
{
    if (updating || plugin)
        return;
    auto* dm = owner.editor.deviceManager();
    if (dm == nullptr)
        return;
    // 합주 중에도 즉시 적용 (스트림 재시작)
    audio::setDevices (*dm, inCombo.getSelectedId() > 0 ? inCombo.getText() : juce::String(),
                       outCombo.getSelectedId() > 0 ? outCombo.getText() : juce::String());
    if (owner.editor.saveSettingsIfNeeded)
        owner.editor.saveSettingsIfNeeded();
    refreshDevices();
}

void SettingsScreen::Content::applyBuffer()
{
    if (updating || plugin)
        return;
    auto* dm = owner.editor.deviceManager();
    const int idx = bufCombo.getSelectedId() - 1;
    if (dm == nullptr || idx < 0 || idx >= bufferChoices.size())
        return;
    audio::setBufferSize (*dm, bufferChoices[idx]);
    if (owner.editor.saveSettingsIfNeeded)
        owner.editor.saveSettingsIfNeeded();
    refreshDevices();
}

juce::String SettingsScreen::Content::earGuardDescription (int level)
{
    switch ((EarGuard::Level) level)
    {
        case EarGuard::Level::Off:    return TXT ("갑자기 큰 소리가 나도 줄여주지 않아요");
        case EarGuard::Level::Normal: return TXT ("귀를 찌르는 소리만 살짝 눌러줘요. 평소 소리는 그대로예요.");
        case EarGuard::Level::Strong: return TXT ("큰 소리를 더 일찍, 더 많이 줄여줘요. 소리가 조금 얌전해질 수 있어요.");
    }
    return {};
}

juce::String SettingsScreen::Content::updateTitle() const
{
    auto& u = owner.editor.getUpdater();
    switch (u.getState())
    {
        case MeonUpdater::State::Idle:
        case MeonUpdater::State::Checking:    return TXT ("새 버전을 확인하고 있어요");
        case MeonUpdater::State::UpToDate:    return TXT ("최신 버전이에요");
        case MeonUpdater::State::Available:   return TXT ("새 버전이 있어요");
        case MeonUpdater::State::Downloading:
        {
            const float p = u.getProgress();
            return TXT ("새 버전을 받는 중이에요") + (p >= 0.0f ? " " + juce::String ((int) std::floor (p * 100.0f)) + "%" : juce::String());
        }
        case MeonUpdater::State::Ready:       return TXT ("설치할 준비가 됐어요");
        case MeonUpdater::State::Failed:      return u.lastFailureWasDownload() ? TXT ("새 버전을 받지 못했어요") : TXT ("새 버전을 확인하지 못했어요");
    }
    return {};
}

juce::String SettingsScreen::Content::updateDetail() const
{
    auto& u = owner.editor.getUpdater();
    const auto now = "build " + juce::String (MeonUpdater::currentBuild());
    switch (u.getState())
    {
        case MeonUpdater::State::Available:
        case MeonUpdater::State::Ready:
            return now + TXT (" → build ") + juce::String (u.getLatestBuild())
                   + (inRoom ? TXT (" · 합주가 끝나면 업데이트할 수 있어요") : TXT (" · 업데이트하면 앱이 다시 켜져요"));
        case MeonUpdater::State::Downloading:
            return now + TXT (" → build ") + juce::String (u.getLatestBuild()) + TXT (" · 다 받으면 앱이 다시 켜져요");
        case MeonUpdater::State::Failed:
            return u.lastFailureWasPermission() ? TXT ("Meon 을 응용 프로그램 폴더로 옮긴 뒤 다시 켜 주세요")
                                                : TXT ("인터넷 연결을 확인하고 다시 시도해 주세요");
        default:
            return now;
    }
}

void SettingsScreen::Content::refreshUpdate()
{
    if (! showUpdate)
        return;
    const auto st = owner.editor.getUpdater().getState();
    juce::String label;
    switch (st)
    {
        case MeonUpdater::State::Idle:
        case MeonUpdater::State::Checking:    label = TXT ("확인 중"); break;
        case MeonUpdater::State::Available:   label = TXT ("업데이트"); break;
        case MeonUpdater::State::Downloading: label = TXT ("받는 중"); break;
        case MeonUpdater::State::Ready:       label = TXT ("다시 켜서 설치"); break;
        case MeonUpdater::State::Failed:      label = TXT ("다시 시도"); break;
        case MeonUpdater::State::UpToDate:    label = TXT ("다시 확인"); break;
    }
    const bool install = st == MeonUpdater::State::Available || st == MeonUpdater::State::Ready;
    updateButton.setStyle (install ? MeonButton::Style::Primary : MeonButton::Style::Secondary);
    updateButton.setEnabled (st != MeonUpdater::State::Idle && st != MeonUpdater::State::Checking
                             && st != MeonUpdater::State::Downloading && ! (install && inRoom));
    if (label != updateButton.getLabel())
    {
        updateButton.setLabel (label);
        if (getParentComponent() != nullptr)
            owner.resized();
    }
    repaint (updateArea);
}

void SettingsScreen::Content::saveNickname()
{
    auto name = nickInput.getText().trim();
    if (! MeonSettings::isValidNickname (name))
    {
        nickInput.setText (owner.editor.getSettings().getNickname(), juce::dontSendNotification);
        return;
    }
    owner.editor.getSettings().setNickname (name);
    owner.editor.getSession().applyNicknameChange();
    saveButton.setLabel (TXT ("저장됨"));
    juce::Component::SafePointer<Content> safe (this);
    juce::Timer::callAfterDelay (1500, [safe] { if (safe != nullptr) safe->saveButton.setLabel (TXT ("저장")); });
}

void SettingsScreen::Content::layout (int width, int topPad)
{
    sectionTitles.clear(); rowLabels.clear(); infoRows.clear(); texts.clear(); subTexts.clear(); dividers.clear();
    const int labelW = plugin ? 100 : 120;
    const int ctrlH = plugin ? 38 : 40;
    const float titlePx = plugin ? 14.0f : 15.0f;
    const int titleH = (int) std::ceil (Fonts::get (700, titlePx).getHeight());
    const int sectionGap = plugin ? 24 : 32;
    const int titleGap = plugin ? 12 : 14;
    int y = topPad;   // 위 여백은 스크롤 영역 안에 둔다 (헤더 바로 아래에서 잘리지 않게)

    auto sectionTitle = [&] (const juce::String& t)
    {
        sectionTitles.push_back ({ t, juce::Rectangle<int> (0, y, width, titleH) });
        y += titleH + titleGap;
    };
    auto divider = [&]
    {
        y += sectionGap;
        dividers.push_back (juce::Rectangle<int> (0, y, width, 1));
        y += 1 + sectionGap;
    };

    // 귀 테러 방지 줄: 왼쪽 이름, 오른쪽 탭, 그 아래 고른 단계 설명
    auto earGuardRow = [&]
    {
        rowLabels.push_back ({ TXT ("귀 테러 방지"), juce::Rectangle<int> (0, y, labelW, ctrlH) });
        const int tabsW = juce::jmin (width - labelW, juce::jmax (plugin ? 216 : 240, earGuardTabs.getIdealWidth (plugin ? 16 : 18)));
        earGuardTabs.setBounds (labelW, y, tabsW, ctrlH);
        y += ctrlH + 8;
        // 좁은 플러그인 창에서는 긴 설명이 두 줄이 된다
        float longest = 0.0f;
        for (int i = 0; i < 3; ++i)
            longest = juce::jmax (longest, Fonts::get (400, 13.0f).getStringWidthFloat (earGuardDescription (i)));
        const int descH = longest > (float) (width - labelW) ? 36 : 18;
        earGuardDescArea = juce::Rectangle<int> (labelW, y, width - labelW, descH);
        y += descH;
    };

    if (! plugin)
    {
        sectionTitle (TXT ("오디오"));
        auto row = [&] (const juce::String& label, juce::Component& c)
        {
            rowLabels.push_back ({ label, juce::Rectangle<int> (0, y, labelW, ctrlH) });
            c.setBounds (labelW, y, width - labelW, ctrlH);
            y += ctrlH + 12;
        };
        if (! driverChoices.isEmpty())
            row (TXT ("드라이버"), driverCombo);
        row (TXT ("입력"), inCombo);
        row (TXT ("출력"), outCombo);
        row (TXT ("버퍼 크기"), bufCombo);
        if (driverPanelButton.isVisible())
        {
            // ASIO 는 버퍼 크기를 드라이버 설정 창에서 정하는 경우가 많다
            const int bw = driverPanelButton.getIdealWidth (18);
            const int by = bufCombo.getY();
            bufCombo.setBounds (bufCombo.getBounds().withTrimmedRight (bw + 10));
            driverPanelButton.setBounds (width - bw, by, bw, ctrlH);
        }
        rowLabels.push_back ({ TXT ("입력 레벨"), juce::Rectangle<int> (0, y, labelW, ctrlH) });
        meter.setBounds (labelW, y + ctrlH / 2 - 5, width - labelW - 12 - 56, 10);
        levelTextArea = juce::Rectangle<int> (width - 56, y, 56, ctrlH);
        y += ctrlH + 12;
        earGuardRow();
        divider();
    }

    sectionTitle (TXT ("닉네임"));
    {
        const int saveW = saveButton.getIdealWidth (plugin ? 16 : 18);
        nickInput.setBounds (0, y, width - saveW - (plugin ? 8 : 10), ctrlH);
        saveButton.setBounds (width - saveW, y, saveW, ctrlH);
        y += ctrlH;
    }
    y += sectionGap;   // 닉네임과 파트 사이는 구분선 없이 간격만

    sectionTitle (TXT ("파트"));
    {
        const int cellH = plugin ? 72 : 84;
        partSelector.setBounds (0, y, width, cellH);
        y += cellH;
        if (! plugin)
        {
            y += 14;
            subTexts.push_back ({ TXT ("닉네임과 파트는 합주 중에 바꾸면 다음 입장부터 적용돼요"), juce::Rectangle<int> (0, y, width, 16) });
            y += 16;
        }
    }
    divider();

    if (plugin)
    {
        sectionTitle (TXT ("오디오"));
        audioPanel = juce::Rectangle<int> (0, y, width, 14 * 2 + 18 + 6 + 16);
        y += audioPanel.getHeight() + 16;
        earGuardRow();   // 장치·버퍼는 DAW 가 정하지만 귀 테러 방지는 플러그인에서도 고른다
        divider();
    }

    sectionTitle (TXT ("문제 해결"));
    {
        const int btnW = logButton.getIdealWidth (plugin ? 16 : 18);
        texts.push_back ({ plugin ? TXT ("연결이나 소리에 문제가 있을 때") : TXT ("연결이나 소리에 문제가 있을 때 로그를 보내주세요"), juce::Rectangle<int> (0, y, width - btnW - 16, 18) });
        subTexts.push_back ({ MeonSettings::getLogFolder().getFullPathName(), juce::Rectangle<int> (0, y + 18 + (plugin ? 3 : 4), width - btnW - 16, 16) });
        logButton.setBounds (width - btnW, y + (18 + 4 + 16) / 2 - ctrlH / 2, btnW, ctrlH);
        y += 18 + 4 + 16;
    }
    divider();

    if (showUpdate)
    {
        // 문제 해결과 같은 모양: 왼쪽 두 줄(상태 · 빌드), 오른쪽 버튼
        sectionTitle (TXT ("업데이트"));
        const int btnW = updateButton.getIdealWidth (18);
        updateArea = juce::Rectangle<int> (0, y, width - btnW - 16, 18 + 4 + 16);
        updateButton.setBounds (width - btnW, y + (18 + 4 + 16) / 2 - ctrlH / 2, btnW, ctrlH);
        y += 18 + 4 + 16;
        divider();
    }

    sectionTitle (TXT ("앱 정보"));
    {
        const float px = plugin ? 13.0f : 14.0f;
        const int rowH = (int) std::ceil (Fonts::get (400, px).getHeight());
        const int rowGap = plugin ? 7 : 8;
        infoRows.push_back ({ TXT ("버전"), juce::Rectangle<int> (0, y, width, rowH) });
        y += rowH + rowGap;
        infoRows.push_back ({ TXT ("기반"), juce::Rectangle<int> (0, y, width, rowH) });
        y += rowH + rowGap;
        infoRows.push_back ({ TXT ("라이선스"), juce::Rectangle<int> (0, y, width, rowH) });
        auto font = Fonts::get (400, px);
        const juce::String prefix = plugin ? TXT ("GPLv3 · ") : TXT ("GPLv3 — 소스 코드 공개 의무가 적용됩니다. ");
        int lx = labelW + (int) std::ceil (font.getStringWidthFloat (prefix));
        licenseLink.setBounds (lx, y, licenseLink.getIdealWidth(), rowH);
        if (! plugin)
        {
            lx = licenseLink.getRight() + (int) std::ceil (font.getStringWidthFloat (TXT (" · ")));
            sourceLink.setBounds (lx, y, sourceLink.getIdealWidth(), rowH);
        }
        y += rowH;
    }
    setSize (width, y);
}

void SettingsScreen::Content::paint (juce::Graphics& g)
{
    const float titlePx = plugin ? 14.0f : 15.0f;
    const int labelW = plugin ? 100 : 120;
    for (auto& t : sectionTitles)
        drawSpacedText (g, t.first, Fonts::get (700, titlePx), titlePx, plugin ? 0.0f : 0.02f, col::ink, t.second.toFloat(), juce::Justification::centredLeft);
    g.setColour (col::cardBorder);
    for (auto& d : dividers)
        g.fillRect (d);
    g.setFont (Fonts::get (400, 14.0f));
    g.setColour (col::inkSub);
    for (auto& r : rowLabels)
        g.drawText (r.first, r.second, juce::Justification::centredLeft, false);
    if (! plugin)
    {
        g.setFont (Fonts::get (500, 13.0f));
        const float db = meter.getShownDb();
        g.drawText (db <= -60.0f ? juce::String ("-60 dB") : juce::String ((int) std::lround (db)) + " dB", levelTextArea, juce::Justification::centredRight, false);
    }
    if (! earGuardDescArea.isEmpty())
    {
        g.setFont (Fonts::get (400, 13.0f));
        g.drawFittedText (earGuardDescription (earGuardTabs.getSelected()), earGuardDescArea, juce::Justification::topLeft, 2, 1.0f);
    }
    g.setColour (col::ink);
    g.setFont (Fonts::get (400, plugin ? 14.0f : 15.0f));
    for (auto& t : texts)
        g.drawText (t.first, t.second, juce::Justification::centredLeft, true);
    g.setColour (col::disabled);
    g.setFont (Fonts::get (400, plugin ? 12.0f : 13.0f));
    for (auto& t : subTexts)
        g.drawText (t.first, t.second, juce::Justification::centredLeft, true);

    if (showUpdate && ! updateArea.isEmpty())
    {
        g.setColour (col::ink);
        g.setFont (Fonts::get (400, 15.0f));
        g.drawText (updateTitle(), updateArea.withHeight (18), juce::Justification::centredLeft, true);
        g.setColour (col::disabled);
        g.setFont (Fonts::get (400, 13.0f));
        g.drawText (updateDetail(), updateArea.withTrimmedTop (18 + 4).withHeight (16), juce::Justification::centredLeft, true);
    }

    if (plugin && ! audioPanel.isEmpty())
    {
        g.setColour (col::panel);
        g.fillRoundedRectangle (audioPanel.toFloat(), (float) metric::radiusCard);
        g.setColour (col::cardBorder);
        g.drawRoundedRectangle (audioPanel.toFloat().reduced (0.5f), (float) metric::radiusCard, 1.0f);
        auto inner = audioPanel.reduced (16, 14);
        g.setColour (col::inkSub);
        g.setFont (Fonts::get (400, 14.0f));
        g.drawText (TXT ("장치와 버퍼는 DAW 설정을 따릅니다"), inner.removeFromTop (18), juce::Justification::centredLeft, false);
        inner.removeFromTop (6);
        const double sr = owner.editor.getProcessor().getSampleRate();
        const int bs = owner.editor.getProcessor().getCurrSamplesPerBlock();   // 실제 블록 크기 (Logic 은 I/R 을 켜면 prepareToPlay 없이 바꾼다)
        const double ms = sr > 0.0 ? bs / sr * 1000.0 : 0.0;
        g.setColour (col::inkSub);
        g.setFont (Fonts::get (400, 13.0f));
        g.drawText (owner.editor.getHostDescription() + TXT (" · ") + juce::String (sr / 1000.0, (sr >= 1000.0 && std::fmod (sr, 1000.0) == 0.0) ? 0 : 1) + " kHz"
                    + TXT (" · ") + juce::String (bs) + TXT (" 샘플") + " (" + juce::String (ms, 1) + " ms)",
                    inner.removeFromTop (16), juce::Justification::centredLeft, false);
    }

    // 앱 정보
    const float px = plugin ? 13.0f : 14.0f;
    auto font = Fonts::get (400, px);
    g.setFont (font);
    int i = 0;
    for (auto& r : infoRows)
    {
        g.setColour (col::inkSub);
        g.drawText (r.first, r.second.withWidth (labelW), juce::Justification::centredLeft, false);
        g.setColour (col::inkSub);
        juce::String value;
        if (i == 0)      value = owner.editor.getVersionText();
        else if (i == 1) value = TXT ("SonoBus 기반 수정판");
        else             value = plugin ? TXT ("GPLv3 · ") : TXT ("GPLv3 — 소스 코드 공개 의무가 적용됩니다. ");
        g.drawText (value, r.second.withTrimmedLeft (labelW), juce::Justification::centredLeft, false);
        if (i == 2 && ! plugin)
            g.drawText (TXT (" · "), juce::Rectangle<int> (licenseLink.getRight(), r.second.getY(), sourceLink.getX() - licenseLink.getRight(), r.second.getHeight()), juce::Justification::centredLeft, false);
        ++i;
    }
}

//==============================================================================
SettingsScreen::SettingsScreen (MeonEditor& e)
    : editor (e), plugin (e.isPluginMode()), closeButton (TXT ("닫기")), content (*this)
{
    closeButton.setFont (plugin ? 12.0f : 13.0f, 500);
    closeButton.onClick = [this] { editor.closeSettings(); };
    addAndMakeVisible (closeButton);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false, false, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
    if (auto* dm = editor.deviceManager())
        dm->addChangeListener (this);
    if (content.showUpdate)
        editor.getUpdater().addChangeListener (this);
    lastTickMs = juce::Time::getMillisecondCounterHiRes();
    if (! plugin)
        vblank = std::make_unique<juce::VBlankAttachment> (this, [this] { meterFrame(); });
    startTimerHz (30);
}

SettingsScreen::~SettingsScreen()
{
    if (auto* dm = editor.deviceManager())
        dm->removeChangeListener (this);
    editor.getUpdater().removeChangeListener (this);
}

void SettingsScreen::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &editor.getUpdater())
        content.refreshUpdate();
    else
        content.refreshDevices();
}

void SettingsScreen::timerCallback()
{
    if (plugin)
    {
        // 호스트 블록 크기가 바뀌면 오디오 칸을 다시 그린다
        const int bs = editor.getProcessor().getCurrSamplesPerBlock();
        if (bs != lastBlockSize)
        {
            lastBlockSize = bs;
            content.repaint();
        }
        return;
    }
    if (content.showUpdate && content.inRoom != editor.getSession().isInRoom())
    {
        content.inRoom = editor.getSession().isInRoom();   // 합주 중에는 업데이트 버튼을 막는다
        content.refreshUpdate();
    }
}

// 레벨 막대는 화면 주사율에 맞춰 (VBlank) 움직인다
void SettingsScreen::meterFrame()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = juce::jlimit (0.0, 0.2, (now - lastTickMs) * 0.001);
    lastTickMs = now;
    const int ch = editor.getSettings().getInputChannelStart();
    float db = editor.getSession().getMyInputLevelDb (ch);
    if (editor.getSettings().getInputChannelCount() == 2)
        db = juce::jmax (db, editor.getSession().getMyInputLevelDb (ch + 1));
    content.pushLevel (db, dt);
}

void SettingsScreen::showPopupForTest (const juce::String& which)
{
    juce::ComboBox* box = which == "input"  ? &content.inCombo
                        : which == "output" ? &content.outCombo
                        : which == "buffer" ? &content.bufCombo : nullptr;
    if (box != nullptr && box->isEnabled())
    {
        box->showPopup();
        box->repaint();
    }
}

bool SettingsScreen::handleShortcut (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        editor.closeSettings();
        return true;
    }
    return false;
}

void SettingsScreen::resized()
{
    auto r = getLocalBounds();
    const int headerH = plugin ? 48 : 60;
    auto header = r.removeFromTop (headerH);
    const int cw = closeButton.getIdealWidth (plugin ? 14 : 16);
    closeButton.setBounds (header.getRight() - (plugin ? 14 : 20) - cw, header.getCentreY() - (plugin ? 15 : 17), cw, plugin ? 30 : 34);

    const int padTop = plugin ? 28 : 40, padBottom = plugin ? 36 : 48;
    const int w = plugin ? 560 : 720;
    content.layout (w, padTop);
    content.setSize (w, content.getHeight() + padBottom);   // 아래 여백
    viewport.setBounds (r.getCentreX() - w / 2, r.getY(), w + 12, r.getHeight());
}

void SettingsScreen::paint (juce::Graphics& g)
{
    ScreenBase::paint (g);
    const int headerH = plugin ? 48 : 60;
    g.setColour (col::cardBorder);
    g.fillRect (0, headerH - 1, getWidth(), 1);
    g.setColour (col::ink);
    g.setFont (Fonts::get (700, plugin ? 17.0f : 20.0f));
    g.drawText (TXT ("설정"), juce::Rectangle<int> (plugin ? 14 : 20, 0, 200, headerH), juce::Justification::centredLeft, false);
}

} // namespace meon
