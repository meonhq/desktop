// MEON core - juce::var(JSON) ↔ API 구조체. 진짜 서버와 가짜 서버가 같은 변환을 지난다.
// decode 는 필수 필드가 없거나 타입이 다르면 false. null 로 올 수 있는 필드는 빠져 있어도 null 로 본다.
#pragma once

#include "ApiTypes.h"
#include "ApiError.h"

namespace meon::core::json
{

juce::var encode (const UserPublic&);
juce::var encode (const MeSettings&);
juce::var encode (const Me&);
juce::var encode (const RoomRef&);
juce::var encode (const UserProfile&);
juce::var encode (const RoomMember&);
juce::var encode (const Room&);
juce::var encode (const Connection&);
juce::var encode (const RoomDetail&);
juce::var encode (const FriendRequest&);
juce::var encode (const Friend&);
juce::var encode (const InviteRoom&);
juce::var encode (const Invite&);
juce::var encode (const JoinRequest&);
juce::var encode (const Block&);
juce::var encode (const Config&);
juce::var encode (const Hello&);
juce::var encode (const MePatch&);
juce::var encode (const CreateRoom&);
juce::var encode (const RoomPatch&);

bool decode (const juce::var&, UserPublic&);
bool decode (const juce::var&, MeSettings&);
bool decode (const juce::var&, Me&);
bool decode (const juce::var&, RoomRef&);
bool decode (const juce::var&, UserProfile&);
bool decode (const juce::var&, RoomMember&);
bool decode (const juce::var&, Room&);
bool decode (const juce::var&, Connection&);
bool decode (const juce::var&, RoomDetail&);
bool decode (const juce::var&, FriendRequest&);
bool decode (const juce::var&, Friend&);
bool decode (const juce::var&, InviteRoom&);
bool decode (const juce::var&, Invite&);
bool decode (const juce::var&, JoinRequest&);
bool decode (const juce::var&, Block&);
bool decode (const juce::var&, Config&);
bool decode (const juce::var&, Hello&);
bool decode (const juce::var&, MePatch&);
bool decode (const juce::var&, CreateRoom&);
bool decode (const juce::var&, RoomPatch&);

template <typename T>
juce::var encodeList (const std::vector<T>& items)
{
    juce::Array<juce::var> arr;
    for (auto& i : items)
        arr.add (encode (i));
    return arr;
}

template <typename T>
bool decodeList (const juce::var& v, std::vector<T>& out)
{
    out.clear();
    auto* arr = v.getArray();
    if (arr == nullptr) return false;
    for (auto& item : *arr)
    {
        T t;
        if (! decode (item, t)) return false;
        out.push_back (std::move (t));
    }
    return true;
}

/** 오류 본문 {"error":{"code","message","field"}} */
juce::var encodeError (const ApiError&);
ApiError decodeError (int httpStatus, const juce::var& body);

/** 작은 객체를 만들 때: obj ({ {"available", true} }) */
juce::var obj (std::initializer_list<std::pair<const char*, juce::var>>);

/** 시각 ↔ ISO 8601 UTC ("2026-10-13T07:33:00Z", 초 단위) */
juce::String isoTime (juce::int64 millisSinceEpoch);
juce::int64 parseIsoTime (const juce::String&);   // 실패하면 0

} // namespace meon::core::json
