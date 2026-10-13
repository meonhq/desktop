#include "OnboardingScreens.h"
#include "MeonAudio.h"

namespace meon
{

//==============================================================================
OnboardingPage::OnboardingPage (MeonEditor& e, int step, int totalSteps, const juce::String& title, const juce::String& desc)
    : editor (e), plugin (e.isPluginMode()),
      steps (totalSteps, step, e.isPluginMode() ? 40 : 44),
      titleLabel (title, e.isPluginMode() ? 28.0f : 34.0f, 700, col::ink),
      descLabel (desc, e.isPluginMode() ? 15.0f : 16.0f, 400, col::inkSub),
      footerNote ({}, 13.0f, 400, col::disabled),
      footerHint ({}, 13.0f, 400, col::disabled, juce::Justification::centredRight),
      backButton (TXT ("뒤로"), MeonButton::Style::Secondary),
      nextButton (TXT ("다음"), MeonButton::Style::Primary)
{
    if (plugin)
        m = { 40, 48, 32, 40, 28.0f, 26, 10, 15.0f, 18, 42, 20, 26, 15.0f };
    else
        m = { 56, 72, 40, 44, 34.0f, 36, 12, 16.0f, 24, 44, 22, 28, 16.0f };

    descLabel.setLineHeight (1.6f);
    backButton.setFont (m.btnPx, 500);
    nextButton.setFont (m.btnPx, 600);

    addAndMakeVisible (steps);
    addAndMakeVisible (titleLabel);
    addAndMakeVisible (descLabel);
    addAndMakeVisible (footerLine);
    addAndMakeVisible (footerNote);
    addAndMakeVisible (footerHint);
    addChildComponent (backButton);
    addAndMakeVisible (nextButton);
}

void OnboardingPage::resized()
{
    auto r = getLocalBounds().withTrimmedTop (m.padTop).withTrimmedBottom (m.padBottom).reduced (m.padSide, 0);

    steps.setBounds (r.getX(), r.getY(), steps.getPreferredWidth(), 16);

    const int titleY = r.getY() + 16 + m.titleTop;
    auto titleFont = Fonts::get (700, m.titlePx);
    titleLabel.setBounds (r.getX(), titleY, r.getWidth(), (int) std::ceil (titleFont.getHeight()));

    const int descY = titleLabel.getBottom() + m.titleGap;
    auto descFont = Fonts::get (400, m.descPx);
    const int descH = (int) std::ceil (paragraphHeight (descLabel.getText(), descFont, (float) r.getWidth(), m.descPx * 1.6f));
    descLabel.setBounds (r.getX(), descY, r.getWidth(), descH);

    // 하단: 구분선 1px + 여백 + 버튼
    const int buttonsY = r.getBottom() - m.btnH;
    footerLine.setBounds (r.getX(), buttonsY - m.footerPad - 1, r.getWidth(), 1);

    backButton.setBounds (r.getX(), buttonsY, backButton.getIdealWidth (m.backPadX), m.btnH);
    const int nextW = nextButton.getIdealWidth (m.nextPadX);
    nextButton.setBounds (r.getRight() - nextW, buttonsY, nextW, m.btnH);
    footerNote.setBounds (r.getX(), buttonsY, r.getWidth() / 2, m.btnH);
    footerHint.setBounds (r.getX(), buttonsY, r.getWidth() - nextW - 16, m.btnH);

    layoutBody (juce::Rectangle<int> (r.getX(), descLabel.getBottom(), r.getWidth(), footerLine.getY() - descLabel.getBottom()));
}

//==============================================================================
NicknameScreen::NicknameScreen (MeonEditor& e)
    : OnboardingPage (e, 1, e.isPluginMode() ? 2 : 4,
                      TXT ("이름과 파트를 알려주세요"),
                      e.isPluginMode() ? TXT ("합주 방에서 멤버들에게 보이는 이름과 파트예요.")
                                       : TXT ("합주 방에서 멤버들에게 보이는 이름과 파트예요. 나중에 설정에서 바꿀 수 있어요.")),
      fieldLabel (TXT ("닉네임"), 13.0f, 500, col::inkSub),
      fieldHint (TXT ("한글·영문·숫자·특수문자 2–12자"), 13.0f, 400, col::disabled),
      partLabel (TXT ("파트"), 13.0f, 500, col::inkSub),
      input (e.isPluginMode() ? 16.0f : 17.0f),
      partSelector (e.isPluginMode() ? PartSelector::Style { 8, 24.0f, 8, 14.0f, 600 }
                                     : PartSelector::Style { 10, 28.0f, 10, 15.0f, 600 })
{
    footerNote.setText (plugin ? TXT ("오디오 장치는 DAW 설정을 따릅니다") : TXT ("계정이나 로그인은 필요 없어요"));
    addAndMakeVisible (fieldLabel);
    addAndMakeVisible (input);
    if (! plugin)
        addAndMakeVisible (fieldHint);
    addAndMakeVisible (partLabel);
    addAndMakeVisible (partSelector);

    // 첫 실행은 기본 선택 없음 (이미 고른 적 있으면 그 값)
    partSelector.setSelected (parts::fromKey (editor.getSettings().getPartKey()));
    partSelector.onChange = [this] (Part) { validate(); };

    input.setText (editor.getSettings().getNickname(), juce::dontSendNotification);
    input.setInputRestrictions (12);
    input.onTextChange = [this] { validate(); };
    input.onReturnKey = [this] { submit(); };
    nextButton.onClick = [this] { submit(); };
    validate();
}

void NicknameScreen::parentHierarchyChanged()
{
    if (isShowing())
    {
        juce::Component::SafePointer<NicknameScreen> safe (this);
        juce::Timer::callAfterDelay (60, [safe] { if (safe != nullptr) safe->input.grabKeyboardFocus(); });
    }
}

void NicknameScreen::validate()
{
    // 닉네임 2자 이상 + 파트 선택 시 다음 활성
    nextButton.setEnabled (MeonSettings::isValidNickname (input.getText()) && partSelector.getSelected() != Part::None);
}

void NicknameScreen::submit()
{
    if (! MeonSettings::isValidNickname (input.getText()) || partSelector.getSelected() == Part::None)
        return;
    editor.getSettings().setPartKey (parts::key (partSelector.getSelected()));
    editor.getSettings().setNickname (input.getText().trim());
    editor.getSession().start();
    editor.go (plugin ? MeonEditor::Screen::Headphone : MeonEditor::Screen::Audio);
}

void NicknameScreen::layoutBody (juce::Rectangle<int> body)
{
    const int w = plugin ? 440 : 520;
    const int inputH = plugin ? 44 : 48;
    int y = body.getY() + (plugin ? 28 : 40);
    fieldLabel.setBounds (body.getX(), y, w, 16); y += 16 + 8;
    input.setBounds (body.getX(), y, w, inputH); y += inputH;
    if (! plugin)
    {
        y += 8;
        fieldHint.setBounds (body.getX(), y, w, 16); y += 16;
    }

    // 파트: 닉네임 아래 32(플러그인 24) · 6칸 균등
    const int partW = plugin ? 600 : 680, cellH = plugin ? 72 : 88;
    y += plugin ? 24 : 32;
    partLabel.setBounds (body.getX(), y, partW, 16); y += 16 + 8;
    partSelector.setBounds (body.getX(), y, partW, cellH);
}

//==============================================================================
AudioDeviceScreen::AudioDeviceScreen (MeonEditor& e)
    : OnboardingPage (e, 2, 4,
                      TXT ("오디오 장치를 골라주세요"),
                      TXT ("지연을 줄이려면 오디오 인터페이스를 쓰는 것이 좋아요.")),
      rescanButton (TXT ("장치 다시 검색")),
      asioGuideButton (TXT ("ASIO 설치 안내 보기")),
      driverLabel (TXT ("드라이버"), 13.0f, 500, col::inkSub),
      inLabel (TXT ("입력 장치"), 13.0f, 500, col::inkSub),
      outLabel (TXT ("출력 장치"), 13.0f, 500, col::inkSub),
      bufLabel (TXT ("버퍼 크기"), 13.0f, 500, col::inkSub),
      bufHint (TXT ("작을수록 지연이 줄지만 잡음이 생길 수 있어요"), 13.0f, 400, col::disabled, juce::Justification::centredRight)
{
    showBack (true);
    backButton.onClick = [this] { editor.go (MeonEditor::Screen::Nickname); };
    nextButton.onClick = [this] { editor.go (MeonEditor::Screen::Headphone); };

    warning.setButtonsHeight (34);
    rescanButton.setFont (13.0f, 500);
    asioGuideButton.setFont (13.0f, 500);
    rescanButton.onClick = [this] { if (auto* t = deviceType()) t->scanForDevices(); refreshDevices(); };
    asioGuideButton.onClick = [] { juce::URL ("https://github.com/meonhq/desktop/blob/main/doc/INSTALL.md#windows-asio").launchInDefaultBrowser(); };
    addChildComponent (warning);
    warning.addChildComponent (rescanButton);
    warning.addChildComponent (asioGuideButton);

    if (auto* dm = editor.deviceManager())
        driverChoices = audio::driverTypes (*dm);
    if (! driverChoices.isEmpty())
    {
        addAndMakeVisible (driverLabel);
        addAndMakeVisible (driverCombo);
        driverCombo.onChange = [this] { applyDriver(); };
    }
    addAndMakeVisible (inLabel);
    addAndMakeVisible (outLabel);
    addAndMakeVisible (bufLabel);
    addAndMakeVisible (bufHint);
    addAndMakeVisible (inCombo);
    addAndMakeVisible (outCombo);
    inCombo.onChange = [this] { applySelection(); };
    outCombo.onChange = [this] { applySelection(); };

    for (int i = 0; i < 4; ++i)
    {
        auto* b = bufferButtons.add (new MeonButton (juce::String (bufferSizes[i]), MeonButton::Style::Secondary));
        b->setFont (16.0f, 500);
        b->setSubtitleFont (12.0f, 400);
        b->onClick = [this, i] { if (i < bufferSizes.size()) selectBuffer (bufferSizes[i]); };
        addAndMakeVisible (b);
    }

    if (auto* dm = editor.deviceManager())
        dm->addChangeListener (this);
    refreshDevices();
}

AudioDeviceScreen::~AudioDeviceScreen()
{
    if (auto* dm = editor.deviceManager())
        dm->removeChangeListener (this);
}

juce::AudioIODeviceType* AudioDeviceScreen::deviceType() const
{
    auto* dm = editor.deviceManager();
    return dm != nullptr ? dm->getCurrentDeviceTypeObject() : nullptr;
}

void AudioDeviceScreen::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshDevices();
}

void AudioDeviceScreen::refreshDevices()
{
    auto* dm = editor.deviceManager();
    auto* type = deviceType();
    updating = true;

    juce::StringArray ins, outs;
    if (type != nullptr && dm != nullptr)
    {
        ins = type->getDeviceNames (true);
        outs = type->getDeviceNames (false);
    }

    if (! driverChoices.isEmpty() && dm != nullptr)
    {
        driverCombo.clear (juce::dontSendNotification);
        for (int i = 0; i < driverChoices.size(); ++i)
            driverCombo.addItem (audio::driverLabel (driverChoices[i]), i + 1);
        const int di = driverChoices.indexOf (dm->getCurrentAudioDeviceType());
        if (di >= 0)
            driverCombo.setSelectedId (di + 1, juce::dontSendNotification);
    }

    noDevices = (ins.isEmpty() && outs.isEmpty());
    const juce::String none = TXT ("장치를 찾을 수 없음");

    auto fill = [&none] (juce::ComboBox& box, const juce::StringArray& names, const juce::String& current)
    {
        box.clear (juce::dontSendNotification);
        if (names.isEmpty())
        {
            box.setTextWhenNothingSelected (none);
            box.setEnabled (false);
            return;
        }
        box.setEnabled (true);
        for (int i = 0; i < names.size(); ++i)
            box.addItem (names[i], i + 1);
        const int idx = names.indexOf (current);
        if (idx >= 0)
            box.setSelectedId (idx + 1, juce::dontSendNotification);
        else
            box.setTextWhenNothingSelected (none);
    };

    auto setup = dm != nullptr ? dm->getAudioDeviceSetup() : juce::AudioDeviceManager::AudioDeviceSetup();
    fill (inCombo, ins, setup.inputDeviceName);
    fill (outCombo, outs, setup.outputDeviceName);

#if JUCE_WINDOWS
    // ASIO 가 아니어도 합주는 되지만 지연이 더 크다는 것을 알려 준다
    const bool asio = dm != nullptr && dm->getCurrentAudioDeviceType() == "ASIO";
    if (noDevices)
    {
        warning.setTitle (TXT ("오디오 장치를 찾지 못했어요"));
        warning.setLines ({ asio ? TXT ("ASIO 드라이버가 있는 장치가 없어요. 위에서 드라이버를 ‘Windows 오디오’로 바꾸거나, 오디오 인터페이스의 ASIO 드라이버를 설치해 주세요.")
                                 : TXT ("장치를 연결한 뒤 ‘장치 다시 검색’을 눌러 주세요.") });
    }
    else if (! asio)
    {
        warning.setTitle (TXT ("Windows 오디오로 연결했어요"));
        warning.setLines ({ TXT ("이대로도 합주할 수 있지만 지연이 조금 더 커요. 오디오 인터페이스가 있다면 ASIO 드라이버를 쓰는 것이 좋아요.") });
    }
    const bool showWarning = noDevices || ! asio;
    warning.setVisible (showWarning);
    rescanButton.setVisible (showWarning);
    asioGuideButton.setVisible (showWarning);
#else
    warning.setVisible (false);
#endif
    nextButton.setEnabled (! noDevices);
    updateBufferButtons();
    updating = false;
    resized();
}

void AudioDeviceScreen::applyDriver()
{
    if (updating)
        return;
    auto* dm = editor.deviceManager();
    const int idx = driverCombo.getSelectedId() - 1;
    if (dm == nullptr || idx < 0 || idx >= driverChoices.size())
        return;
    audio::setDriverType (*dm, driverChoices[idx]);
    if (editor.saveSettingsIfNeeded)
        editor.saveSettingsIfNeeded();
    refreshDevices();
}

void AudioDeviceScreen::applySelection()
{
    if (updating)
        return;
    auto* dm = editor.deviceManager();
    if (dm == nullptr)
        return;
    audio::setDevices (*dm, inCombo.getSelectedId() > 0 ? inCombo.getText() : juce::String(),
                       outCombo.getSelectedId() > 0 ? outCombo.getText() : juce::String());
    if (editor.saveSettingsIfNeeded)
        editor.saveSettingsIfNeeded();
    refreshDevices();   // ASIO 는 입력·출력이 함께 바뀐다
}

void AudioDeviceScreen::selectBuffer (int size)
{
    auto* dm = editor.deviceManager();
    if (dm == nullptr)
        return;
    audio::setBufferSize (*dm, size);
    if (editor.saveSettingsIfNeeded)
        editor.saveSettingsIfNeeded();
    updateBufferButtons();
}

void AudioDeviceScreen::updateBufferButtons()
{
    auto* dm = editor.deviceManager();
    auto setup = dm != nullptr ? dm->getAudioDeviceSetup() : juce::AudioDeviceManager::AudioDeviceSetup();
    const double sr = setup.sampleRate > 0.0 ? setup.sampleRate : 48000.0;
    const int current = dm != nullptr ? audio::currentBufferSize (*dm) : setup.bufferSize;
    if (dm != nullptr)
        bufferSizes = audio::bufferChoices (*dm);

    for (int i = 0; i < bufferButtons.size(); ++i)
    {
        auto* b = bufferButtons[i];
        b->setVisible (i < bufferSizes.size());
        if (i >= bufferSizes.size())
            continue;
        const int size = bufferSizes[i];
        b->setLabel (juce::String (size));
        const bool sel = (size == current);
        b->setSubtitle (juce::String (size / sr * 1000.0, 1) + " ms");
        if (sel)
        {
            b->setColourOverride (col::warnBg, col::ink, col::accent, col::warnBg);
            b->setFont (16.0f, 600);
            b->setSubtitleInk (col::inkSub);
        }
        else
        {
            b->setColourOverride (col::white, col::inkSub, col::border, col::panel);
            b->setFont (16.0f, 500);
            b->setSubtitleInk (col::disabled);
        }
        b->setEnabled (! noDevices);
    }
}

void AudioDeviceScreen::layoutBody (juce::Rectangle<int> body)
{
    const int w = 680;
    int y = body.getY();
    // Windows: 드라이버 줄이 하나 더 있으므로 간격을 줄여 한 화면에 들어가게 한다
    const bool compact = driverCombo.isVisible();
    const int comboH = compact ? 44 : 48;
    const int rowGap = compact ? 14 : 20;

    if (warning.isVisible())
    {
        y += compact ? 20 : 28;
        const int h = warning.getPreferredHeight (w);
        warning.setBounds (body.getX(), y, w, h);
        const int bx = warning.getTextLeft();
        const int by = warning.getButtonsTop();
        rescanButton.setBounds (bx, by, rescanButton.getIdealWidth (14), 34);
        asioGuideButton.setBounds (rescanButton.getRight() + 10, by, asioGuideButton.getIdealWidth (14), 34);
        y += h;
    }

    y += compact ? 22 : 32;
    if (compact)
    {
        driverLabel.setBounds (body.getX(), y, w, 16); y += 16 + 8;
        driverCombo.setBounds (body.getX(), y, w, comboH); y += comboH + rowGap;
    }
    inLabel.setBounds (body.getX(), y, w, 16); y += 16 + 8;
    inCombo.setBounds (body.getX(), y, w, comboH); y += comboH + rowGap;
    outLabel.setBounds (body.getX(), y, w, 16); y += 16 + 8;
    outCombo.setBounds (body.getX(), y, w, comboH); y += comboH + rowGap;
    bufLabel.setBounds (body.getX(), y, 200, 16);
    bufHint.setBounds (body.getX() + 200, y, w - 200, 16); y += 16 + 8;

    const int gap = 8;
    const int n = juce::jmax (1, juce::jmin (bufferButtons.size(), bufferSizes.size()));
    const int bw = (w - gap * (n - 1)) / n;
    for (int i = 0; i < n; ++i)
        bufferButtons[i]->setBounds (body.getX() + i * (bw + gap), y, bw, 48);
}

//==============================================================================
HeadphoneScreen::CheckRow::CheckRow (bool p) : juce::Button ("headphones"), plugin (p)
{
    setClickingTogglesState (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (false);
}

void HeadphoneScreen::CheckRow::paintButton (juce::Graphics& g, bool, bool)
{
    auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();
    g.setColour (on ? col::warnBg : col::white);
    g.fillRoundedRectangle (r, (float) metric::radiusCard);
    g.setColour (on ? col::accent : col::border);
    g.drawRoundedRectangle (r.reduced (0.5f), (float) metric::radiusCard, 1.0f);

    const float padX = plugin ? 20.0f : 24.0f;
    const float box = plugin ? 24.0f : 26.0f;
    juce::Rectangle<float> cb (r.getX() + padX, r.getCentreY() - box * 0.5f, box, box);
    g.setColour (on ? col::accent : col::white);
    g.fillRoundedRectangle (cb, 5.0f);
    g.setColour (on ? col::accent : col::border);
    g.drawRoundedRectangle (cb.reduced (0.5f), 5.0f, 1.0f);
    if (on)
    {
        juce::Path p;
        p.startNewSubPath (cb.getX() + box * 0.26f, cb.getY() + box * 0.52f);
        p.lineTo (cb.getX() + box * 0.44f, cb.getY() + box * 0.70f);
        p.lineTo (cb.getX() + box * 0.76f, cb.getY() + box * 0.32f);
        g.setColour (col::onAccent);
        g.strokePath (p, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setColour (col::ink);
    g.setFont (Fonts::get (600, plugin ? 17.0f : 18.0f));
    g.drawText (TXT ("헤드폰을 쓰고 있어요"),
                juce::Rectangle<float> (cb.getRight() + (plugin ? 14.0f : 16.0f), r.getY(), r.getWidth(), r.getHeight()),
                juce::Justification::centredLeft, false);
}

HeadphoneScreen::HeadphoneScreen (MeonEditor& e)
    : OnboardingPage (e, e.isPluginMode() ? 2 : 3, e.isPluginMode() ? 2 : 4,
                      TXT ("헤드폰을 써주세요"),
                      e.isPluginMode() ? TXT ("스피커로 들으면 다른 사람 소리가 마이크로 다시 들어가 하울링이 생겨요.")
                                       : TXT ("스피커로 들으면 다른 사람 소리가 내 마이크로 다시 들어가 하울링이 생겨요.")),
      changeOutputButton (TXT ("출력 장치 바꾸기")),
      row (e.isPluginMode())
{
    showBack (true);
    backButton.onClick = [this] { editor.go (plugin ? MeonEditor::Screen::Nickname : MeonEditor::Screen::Audio); };
    if (plugin)
        nextButton.setLabel (TXT ("시작하기"));
    nextButton.onClick = [this]
    {
        if (! row.getToggleState())
            return;
        if (plugin)
        {
            editor.getSettings().setOnboardingDone (true, true);
            editor.go (MeonEditor::Screen::Home);
        }
        else
        {
            editor.go (MeonEditor::Screen::Level);
        }
    };

    warning.setLines ({ TXT ("헤드폰을 연결하거나, 이전 단계에서 출력 장치를 바꿔주세요.") });
    warning.setButtonsHeight (34);
    changeOutputButton.setFont (13.0f, 500);
    changeOutputButton.onClick = [this] { editor.go (MeonEditor::Screen::Audio); };
    addChildComponent (warning);
    warning.addAndMakeVisible (changeOutputButton);

    addAndMakeVisible (row);
    row.onClick = [this] { updateState(); };

    if (! plugin)
    {
        checkOutputDevice();
        startTimer (1000);
    }
    updateState();
}

void HeadphoneScreen::checkOutputDevice()
{
    bool warn = false;
    juce::String name;
    if (auto* dm = editor.deviceManager())
    {
        name = dm->getAudioDeviceSetup().outputDeviceName;
#if JUCE_MAC
        const auto lower = name.toLowerCase();
        warn = lower.contains ("speaker") || name.contains (TXT ("스피커"))
               || lower.contains ("built-in output") || name.contains (TXT ("내장 출력"));
#endif
    }
    if (warn != speakerWarning || name != outputName)
    {
        speakerWarning = warn;
        outputName = name;
        warning.setTitle (TXT ("지금 출력이 ‘") + outputName + TXT ("’예요"));
        warning.setVisible (speakerWarning);
        resized();
    }
}

void HeadphoneScreen::updateState()
{
    const bool on = row.getToggleState();
    nextButton.setEnabled (on);
    footerHint.setText ((on || plugin) ? juce::String() : TXT ("체크해야 넘어갈 수 있어요"));
}

void HeadphoneScreen::layoutBody (juce::Rectangle<int> body)
{
    const int w = plugin ? 600 : 680;
    int y = body.getY();
    if (warning.isVisible())
    {
        y += 28;
        const int h = warning.getPreferredHeight (w);
        warning.setBounds (body.getX(), y, w, h);
        changeOutputButton.setBounds (warning.getTextLeft(), warning.getButtonsTop(), changeOutputButton.getIdealWidth (14), 34);
        y += h;
    }
    y += plugin ? 28 : 32;
    row.setBounds (body.getX(), y, w, plugin ? 62 : 72);
}

//==============================================================================
InputLevelScreen::InputLevelScreen (MeonEditor& e)
    : OnboardingPage (e, 4, 4,
                      TXT ("소리를 내보세요"),
                      TXT ("평소 연주하는 세기로 소리를 내면, 아래 막대가 움직여야 해요.")),
      meterLabel ({}, 13.0f, 500, col::inkSub),
      meterValue ({}, 15.0f, 600, col::ink, juce::Justification::centredRight),
      changeChannelButton (TXT ("입력 채널 바꾸기")),
      changeDeviceButton (TXT ("장치 다시 고르기"))
{
    showBack (true);
    backButton.onClick = [this] { editor.go (MeonEditor::Screen::Headphone); };
    nextButton.setLabel (TXT ("시작하기"));
    nextButton.onClick = [this]
    {
        editor.getSettings().setOnboardingDone (false, true);
        editor.go (MeonEditor::Screen::Home);
    };

    meter.setCornerRadius (4.0f);
    addAndMakeVisible (meterLabel);
    addAndMakeVisible (meterValue);
    addAndMakeVisible (meter);

    warning.setTitle (TXT ("입력 신호가 들어오지 않아요"));
    warning.setLines ({ TXT ("· 케이블이 인터페이스 입력 1에 꽂혀 있는지 확인해 주세요"),
                        TXT ("· 인터페이스의 게인 노브를 올려보세요"),
                        TXT ("· 콘덴서 마이크라면 +48V 팬텀 전원을 켜주세요") }, 1.8f);
    warning.setButtonsHeight (34);
    changeChannelButton.setFont (13.0f, 500);
    changeDeviceButton.setFont (13.0f, 500);
    changeChannelButton.onClick = [this] { showChannelMenu(); };
    changeDeviceButton.onClick = [this] { editor.go (MeonEditor::Screen::Audio); };
    addChildComponent (warning);
    warning.addAndMakeVisible (changeChannelButton);
    warning.addAndMakeVisible (changeDeviceButton);

    lastSignalMs = juce::Time::getMillisecondCounterHiRes();
    lastTickMs = lastSignalMs;
    updateLabels();
}

juce::String InputLevelScreen::channelText() const
{
    auto& s = editor.getSettings();
    const int start = s.getInputChannelStart();
    const int count = s.getInputChannelCount();
    juce::String txt = TXT ("입력 ") + juce::String (start + 1);
    if (count == 2)
        txt += "+" + juce::String (start + 2);
    return txt;
}

void InputLevelScreen::updateLabels()
{
    juce::String device;
    if (auto* dm = editor.deviceManager())
        device = dm->getAudioDeviceSetup().inputDeviceName;
    meterLabel.setText (TXT ("입력 레벨 — ") + device + TXT (" · ") + channelText());

    if (noSignal)
    {
        meterValue.setText (TXT ("신호 없음"));
        meterValue.setInk (col::inkSub);
    }
    else
    {
        meterValue.setText (peakDb <= -60.0f ? juce::String ("-60 dB") : juce::String (peakDb, 1) + " dB");
        meterValue.setInk (col::ink);
    }
    footerHint.setText (noSignal ? TXT ("신호가 없어도 넘어갈 수 있어요") : juce::String());
}

void InputLevelScreen::onFrame()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = juce::jlimit (0.0, 0.2, (now - lastTickMs) * 0.001);
    lastTickMs = now;

    const int ch = editor.getSettings().getInputChannelStart();
    float db = editor.getSession().getMyInputLevelDb (ch);
    if (editor.getSettings().getInputChannelCount() == 2)
        db = juce::jmax (db, editor.getSession().getMyInputLevelDb (ch + 1));

    meter.push (db, dt);

    if (db > -55.0f)
        lastSignalMs = now;
    if (db >= peakDb || now - peakAtMs > 1500.0)
    {
        peakDb = db;
        peakAtMs = now;
    }

    const bool wasNoSignal = noSignal;
    noSignal = (now - lastSignalMs) > 3000.0;
    if (noSignal != wasNoSignal)
    {
        warning.setVisible (noSignal);
        resized();
        repaint();
    }
    updateLabels();
}

void InputLevelScreen::showChannelMenu()
{
    auto* dm = editor.deviceManager();
    auto* dev = dm != nullptr ? dm->getCurrentAudioDevice() : nullptr;
    if (dev == nullptr)
        return;
    const int n = dev->getActiveInputChannels().countNumberOfSetBits();
    juce::PopupMenu menu;
    for (int i = 0; i < n; ++i)
        menu.addItem (i + 1, TXT ("입력 ") + juce::String (i + 1));
    for (int i = 0; i + 1 < n; i += 2)
        menu.addItem (100 + i, TXT ("입력 ") + juce::String (i + 1) + "+" + juce::String (i + 2) + TXT (" (스테레오)"));

    juce::Component::SafePointer<InputLevelScreen> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&changeChannelButton), [safe] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        int start = 0, count = 1;
        if (result >= 100) { start = result - 100; count = 2; }
        else { start = result - 1; count = 1; }
        safe->editor.getSettings().setInputChannels (start, count);
        safe->editor.getSession().setInputChannels (start, count);
        safe->updateLabels();
    });
}

void InputLevelScreen::layoutBody (juce::Rectangle<int> body)
{
    const int w = 800;
    int y = body.getY() + 40;
    meterLabel.setBounds (body.getX(), y, w - 120, 18);
    meterValue.setBounds (body.getX() + w - 120, y, 120, 18);
    y += 18 + 14;
    meter.setBounds (body.getX(), y, w, 22);
    y += 22 + 14;
    meterBlock = juce::Rectangle<int> (body.getX(), y, w, 16);   // 눈금
    y += 16;

    y += 28;
    if (warning.isVisible())
    {
        const int h = warning.getPreferredHeight (w);
        warning.setBounds (body.getX(), y, w, h);
        const int bx = warning.getTextLeft(), by = warning.getButtonsTop();
        changeChannelButton.setBounds (bx, by, changeChannelButton.getIdealWidth (14), 34);
        changeDeviceButton.setBounds (changeChannelButton.getRight() + 10, by, changeDeviceButton.getIdealWidth (14), 34);
        okRow = {};
    }
    else
    {
        okRow = juce::Rectangle<int> (body.getX(), y, w, 22);
    }
}

void InputLevelScreen::paint (juce::Graphics& g)
{
    ScreenBase::paint (g);

    // dB 눈금 (400 12 #AAAAAA)
    if (! meterBlock.isEmpty())
    {
        g.setFont (Fonts::get (400, 12.0f));
        g.setColour (col::disabled);
        const char* labels[] = { "-60", "-40", "-20", "-12", "-6", "0 dB" };
        for (int i = 0; i < 6; ++i)
        {
            juce::Rectangle<float> area;
            juce::Justification just = juce::Justification::centred;
            if (i == 0)
            {
                area = juce::Rectangle<float> ((float) meterBlock.getX(), (float) meterBlock.getY(), 60.0f, (float) meterBlock.getHeight());
                just = juce::Justification::centredLeft;
            }
            else if (i == 5)
            {
                area = juce::Rectangle<float> ((float) meterBlock.getRight() - 60.0f, (float) meterBlock.getY(), 60.0f, (float) meterBlock.getHeight());
                just = juce::Justification::centredRight;
            }
            else
            {
                const float x = (float) meterBlock.getX() + ((float) i / 5.0f) * (float) meterBlock.getWidth();
                area = juce::Rectangle<float> (x - 30.0f, (float) meterBlock.getY(), 60.0f, (float) meterBlock.getHeight());
            }
            g.drawText (labels[i], area, just, false);
        }
    }

    if (! noSignal && ! okRow.isEmpty())
    {
        juce::Rectangle<float> icon ((float) okRow.getX(), (float) okRow.getY(), 22.0f, 22.0f);
        g.setColour (col::accent);
        g.fillRoundedRectangle (icon, 4.0f);
        juce::Path p;
        p.startNewSubPath (icon.getX() + 6.0f, icon.getY() + 11.5f);
        p.lineTo (icon.getX() + 9.5f, icon.getY() + 15.0f);
        p.lineTo (icon.getX() + 16.0f, icon.getY() + 7.5f);
        g.setColour (col::onAccent);
        g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col::ink);
        g.setFont (Fonts::get (500, 15.0f));
        g.drawText (TXT ("신호가 잘 들어오고 있어요. 최고점이 -12 ~ -6 dB 사이면 알맞아요."),
                    okRow.withTrimmedLeft (22 + 10), juce::Justification::centredLeft, false);
    }
}

} // namespace meon
