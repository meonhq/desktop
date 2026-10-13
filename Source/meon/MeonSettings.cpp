#include "MeonSettings.h"

namespace meon
{

static juce::PropertiesFile::Options makeOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName     = "MEONUser";
    o.filenameSuffix      = ".settings";
    o.osxLibrarySubFolder = "Application Support/MEON";
#if JUCE_LINUX
    o.folderName          = "~/.config/meon";
#elif JUCE_WINDOWS
    o.folderName          = "MEON";
#else
    o.folderName          = "";
#endif
    o.storageFormat       = juce::PropertiesFile::storeAsXML;
    o.millisecondsBeforeSaving = 500;
    return o;
}

juce::File MeonSettings::getSettingsFile()
{
    return makeOptions().getDefaultFile();
}

MeonSettings::MeonSettings()
{
    props = std::make_unique<juce::PropertiesFile> (makeOptions());
}

MeonSettings::~MeonSettings()
{
    save();
}

juce::String MeonSettings::getNickname() const
{
    return props->getValue ("nickname", "");
}

void MeonSettings::setNickname (const juce::String& name)
{
    props->setValue ("nickname", name.trim());
    save();
}

juce::String MeonSettings::getPartKey() const
{
    return props->getValue ("part", "");
}

void MeonSettings::setPartKey (const juce::String& key)
{
    props->setValue ("part", key);
    save();
}

bool MeonSettings::isOnboardingDone (bool plugin) const
{
    return props->getBoolValue (plugin ? "onboardingDonePlugin" : "onboardingDoneApp", false);
}

void MeonSettings::setOnboardingDone (bool plugin, bool done)
{
    props->setValue (plugin ? "onboardingDonePlugin" : "onboardingDoneApp", done);
    save();
}

int MeonSettings::getInputChannelStart() const
{
    return props->getIntValue ("inputChannelStart", 0);
}

int MeonSettings::getInputChannelCount() const
{
    return juce::jlimit (1, 2, props->getIntValue ("inputChannelCount", 1));
}

void MeonSettings::setInputChannels (int start, int count)
{
    props->setValue ("inputChannelStart", juce::jmax (0, start));
    props->setValue ("inputChannelCount", juce::jlimit (1, 2, count));
    save();
}

EarGuard::Level MeonSettings::getEarGuardLevel() const
{
    return (EarGuard::Level) juce::jlimit (0, 2, props->getIntValue ("earGuard", (int) EarGuard::Level::Normal));
}

void MeonSettings::setEarGuardLevel (EarGuard::Level level)
{
    props->setValue ("earGuard", (int) level);
    save();
}

bool MeonSettings::isChatOpen() const
{
    return props->getBoolValue ("chatOpen", true);
}

void MeonSettings::setChatOpen (bool open)
{
    props->setValue ("chatOpen", open);
}

void MeonSettings::save()
{
    props->saveIfNeeded();
}

juce::File MeonSettings::getSettingsFolder()
{
    return makeOptions().getDefaultFile().getParentDirectory();
}

juce::File MeonSettings::getLogFolder()
{
#if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Logs/MEON");
#elif JUCE_WINDOWS
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("MEON").getChildFile ("logs");
#else
    return getSettingsFolder().getChildFile ("logs");
#endif
}

/** 닉네임에 쓸 수 있는 특수문자: 키보드 기호(!@#$ 등)와 한글 입력기의 특수문자(ㅁ+한자: ★♥♪※→① 등).
    제어 문자·보이지 않는 문자·이모지(U+1F000 이후)는 뺀다. '#' 은 이름 뒤에 붙이는 #XXXX 와 겹쳐도
    마지막 '#' 으로 자르므로 괜찮다 (MeonSession::displayNameFor). */
static bool isNicknameSymbol (juce::juce_wchar c)
{
    return (c >= 0x21 && c <= 0x7E)                     // ASCII 기호 (영문·숫자는 위에서 걸러짐)
        || (c >= 0xA1 && c <= 0xBF && c != 0xAD)        // ¡ ¢ £ ¥ § ° ± · ¿ 등 (0xAD 소프트 하이픈 제외)
        || c == 0xD7 || c == 0xF7                       // × ÷
        || (c >= 0x2010 && c <= 0x2027)                 // ‐ – — ‘ ’ “ ” † ‡ • … 등
        || (c >= 0x2030 && c <= 0x205E)                 // ‰ ′ ″ ※ ‼ ⁂ 등
        || (c >= 0x2100 && c <= 0x27BF)                 // ℃ ™ → ∞ ⌘ ① ■ ★ ♥ ♪ ✓ ✿ 등
        || (c >= 0x3001 && c <= 0x303F)                 // 、 。 〈 〉 《 》 「 」 【 】 〜 등 (0x3000 전각 공백 제외)
        || (c >= 0x3200 && c <= 0x33FF)                 // ㈜ ㉠ ㉮ ㎏ ㎡ 등
        || (c >= 0xFF01 && c <= 0xFF5E);                // 전각 기호·영숫자 ！ ＠ ～ 등
}

bool MeonSettings::isValidNickname (const juce::String& nameIn)
{
    auto name = nameIn.trim();
    const int len = name.length();
    if (len < 2 || len > 12)
        return false;

    for (int i = 0; i < len; ++i)
    {
        const juce::juce_wchar c = name[i];
        const bool hangul = (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0x3131 && c <= 0x318E);
        const bool latin  = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        const bool digit  = (c >= '0' && c <= '9');
        if (! (hangul || latin || digit || c == ' ' || isNicknameSymbol (c)))
            return false;
    }
    return true;
}

} // namespace meon
