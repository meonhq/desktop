#include "ServerConfig.h"

namespace meon::core
{

ServerConfig ServerConfig::prod()
{
    ServerConfig c;
    c.kind = Kind::prod;
    c.restBase = "https://api.meon.co.kr/v1";
    c.wsUrl = "wss://api.meon.co.kr/v1/ws";
    c.aooHost = "aoo.meon.co.kr";
    c.aooPort = 10999;
    c.tokenItem = "refresh-token";
    return c;
}

ServerConfig ServerConfig::local()
{
    auto c = fromUrl ("http://localhost:8080");
    c.kind = Kind::local;
    return c;
}

ServerConfig ServerConfig::fake()
{
    ServerConfig c;
    c.kind = Kind::fake;
    c.aooHost = "localhost";   // 가짜 서버가 주는 AOO 주소 (--fake-aoo 로 바꾼다)
    c.aooPort = 10999;
    c.tokenItem = "fake-refresh-token";
    return c;
}

ServerConfig ServerConfig::fromUrl (const juce::String& base)
{
    ServerConfig c;
    c.kind = Kind::custom;
    auto b = base.trim();
    while (b.endsWithChar ('/')) b = b.dropLastCharacters (1);
    if (b.endsWithIgnoreCase ("/v1")) b = b.dropLastCharacters (3);

    const bool tls = b.startsWithIgnoreCase ("https://");
    if (! tls && ! b.startsWithIgnoreCase ("http://"))
        return c;   // restBase 비어 있음 → 호출한 쪽이 거른다

    const auto hostPort = b.fromFirstOccurrenceOf ("://", false, false).upToFirstOccurrenceOf ("/", false, false);
    c.restBase = b + "/v1";
    c.wsUrl = (tls ? "wss://" : "ws://") + b.fromFirstOccurrenceOf ("://", false, false) + "/v1/ws";
    c.aooHost = hostPort.upToLastOccurrenceOf (":", false, false);
    if (c.aooHost.isEmpty()) c.aooHost = hostPort;
    c.aooPort = 10999;
    c.tokenItem = "refresh-token@" + hostPort;
    return c;
}

juce::String ServerConfig::describe() const
{
    switch (kind)
    {
        case Kind::prod:   return "prod " + restBase;
        case Kind::local:  return "local " + restBase;
        case Kind::custom: return "custom " + restBase;
        case Kind::fake:   return "fake";
    }
    return {};
}

ServerConfig ServerConfig::choose (int build, const juce::StringPairArray& dev)
{
    if (build > 0)
        return prod();

    const auto& keys = dev.getAllKeys();
    if (keys.contains ("server"))
    {
        const auto v = dev["server"].trim();
        if (v.equalsIgnoreCase ("prod"))  return prod();
        if (v.equalsIgnoreCase ("local")) return local();
        if (v.equalsIgnoreCase ("fake"))  return fake();
        auto c = fromUrl (v);
        if (c.restBase.isNotEmpty()) return c;
        return local();   // 알아볼 수 없는 값: 기본으로
    }

    // 화면 미리보기·자동 가입은 서버 없이 도는 가짜 서버로 (CI 스냅숏)
    if (keys.contains ("screen") || keys.contains ("dev-signup") || keys.contains ("dev-jam"))
        return fake();

    return local();
}

} // namespace meon::core
