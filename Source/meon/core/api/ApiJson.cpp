#include "ApiJson.h"

namespace meon::core::json
{

namespace
{
    using juce::var;
    using juce::String;

    /** 객체를 읽으면서 필수 필드가 빠졌는지 기억한다 */
    struct Reader
    {
        explicit Reader (const var& v) : obj (v.getDynamicObject()), ok (obj != nullptr) {}

        const juce::DynamicObject* obj;
        bool ok;

        var get (const char* key) const
        {
            return obj != nullptr ? obj->getProperty (juce::Identifier (key)) : var();
        }

        bool has (const char* key) const
        {
            return obj != nullptr && obj->hasProperty (juce::Identifier (key));
        }

        String str (const char* key)
        {
            auto v = get (key);
            if (! v.isString()) { ok = false; return {}; }
            return v.toString();
        }

        OptString optStr (const char* key)
        {
            auto v = get (key);
            if (v.isVoid() || v.isUndefined()) return std::nullopt;
            if (! v.isString()) { ok = false; return std::nullopt; }
            return v.toString();
        }

        int integer (const char* key)
        {
            auto v = get (key);
            if (! (v.isInt() || v.isInt64() || v.isDouble())) { ok = false; return 0; }
            return (int) v;
        }

        OptInt optInt (const char* key)
        {
            auto v = get (key);
            if (v.isVoid() || v.isUndefined()) return std::nullopt;
            if (! (v.isInt() || v.isInt64() || v.isDouble())) { ok = false; return std::nullopt; }
            return (int) v;
        }

        double number (const char* key)
        {
            auto v = get (key);
            if (! (v.isInt() || v.isInt64() || v.isDouble())) { ok = false; return 0.0; }
            return (double) v;
        }

        bool boolean (const char* key)
        {
            auto v = get (key);
            if (! v.isBool()) { ok = false; return false; }
            return (bool) v;
        }

        template <typename T>
        void child (const char* key, T& out)
        {
            if (! decode (get (key), out)) ok = false;
        }

        template <typename T>
        void optChild (const char* key, std::optional<T>& out)
        {
            auto v = get (key);
            if (v.isVoid() || v.isUndefined()) { out.reset(); return; }
            T t;
            if (! decode (v, t)) { ok = false; return; }
            out = std::move (t);
        }

        template <typename T>
        void list (const char* key, std::vector<T>& out)
        {
            if (! decodeList (get (key), out)) ok = false;
        }
    };

    struct Writer
    {
        juce::DynamicObject::Ptr o { new juce::DynamicObject() };

        Writer& set (const char* key, const var& v)         { o->setProperty (juce::Identifier (key), v); return *this; }
        Writer& set (const char* key, const String& v)      { return set (key, var (v)); }
        Writer& set (const char* key, const OptString& v)   { return set (key, v ? var (*v) : var()); }
        Writer& set (const char* key, const OptInt& v)      { return set (key, v ? var (*v) : var()); }
        Writer& set (const char* key, int v)                { return set (key, var (v)); }
        Writer& set (const char* key, bool v)               { return set (key, var (v)); }
        Writer& set (const char* key, double v)             { return set (key, var (v)); }
        Writer& set (const char* key, const char* v)        { return set (key, var (String (v))); }

        template <typename T>
        Writer& setOpt (const char* key, const std::optional<T>& v) { return set (key, v ? encode (*v) : var()); }

        var done() { return var (o.get()); }
    };
}

//==============================================================================
var encode (const UserPublic& u)
{
    return Writer().set ("id", u.id).set ("username", u.username).set ("nickname", u.nickname).set ("part", u.part).done();
}

bool decode (const var& v, UserPublic& u)
{
    Reader r (v);
    u.id = r.str ("id");
    u.username = r.str ("username");
    u.nickname = r.optStr ("nickname");
    u.part = r.optStr ("part");
    return r.ok;
}

var encode (const MeSettings& s)
{
    return Writer().set ("friend_requests", s.friendRequests).set ("room_invites", s.roomInvites).done();
}

bool decode (const var& v, MeSettings& s)
{
    Reader r (v);
    s.friendRequests = r.str ("friend_requests");
    s.roomInvites = r.str ("room_invites");
    return r.ok;
}

var encode (const Me& m)
{
    return Writer().set ("id", m.id).set ("username", m.username).set ("nickname", m.nickname).set ("part", m.part)
                   .set ("email", m.email).set ("email_verified", m.emailVerified).set ("pending_email", m.pendingEmail)
                   .set ("settings", encode (m.settings)).set ("created_at", m.createdAt).done();
}

bool decode (const var& v, Me& m)
{
    Reader r (v);
    m.id = r.str ("id");
    m.username = r.str ("username");
    m.nickname = r.optStr ("nickname");
    m.part = r.optStr ("part");
    m.email = r.str ("email");
    m.emailVerified = r.boolean ("email_verified");
    m.pendingEmail = r.optStr ("pending_email");
    r.child ("settings", m.settings);
    m.createdAt = r.str ("created_at");
    return r.ok;
}

var encode (const RoomRef& x)
{
    return Writer().set ("id", x.id).set ("title", x.title).set ("joinable", x.joinable).done();
}

bool decode (const var& v, RoomRef& x)
{
    Reader r (v);
    x.id = r.str ("id");
    x.title = r.str ("title");
    x.joinable = r.boolean ("joinable");
    return r.ok;
}

var encode (const UserProfile& p)
{
    return Writer().set ("user", encode (p.user)).set ("relation", p.relation).set ("status", p.status)
                   .set ("last_seen_at", p.lastSeenAt).setOpt ("room", p.room)
                   .set ("can_send_friend_request", p.canSendFriendRequest).set ("can_invite", p.canInvite).done();
}

bool decode (const var& v, UserProfile& p)
{
    Reader r (v);
    r.child ("user", p.user);
    p.relation = r.str ("relation");
    p.status = r.optStr ("status");
    p.lastSeenAt = r.optStr ("last_seen_at");
    r.optChild ("room", p.room);
    p.canSendFriendRequest = r.boolean ("can_send_friend_request");
    p.canInvite = r.boolean ("can_invite");
    return r.ok;
}

var encode (const RoomMember& m)
{
    return Writer().set ("user", encode (m.user)).set ("is_host", m.isHost).set ("is_friend", m.isFriend)
                   .set ("joined_at", m.joinedAt).done();
}

bool decode (const var& v, RoomMember& m)
{
    Reader r (v);
    r.child ("user", m.user);
    m.isHost = r.boolean ("is_host");
    m.isFriend = r.boolean ("is_friend");
    m.joinedAt = r.str ("joined_at");
    return r.ok;
}

static Writer& writeRoom (Writer& w, const Room& x)
{
    return w.set ("id", x.id).set ("type", x.type).set ("entry_mode", x.entryMode).set ("title", x.title)
            .set ("description", x.description).set ("capacity", x.capacity).set ("member_count", x.memberCount)
            .set ("host_id", x.hostId).set ("host_present", x.hostPresent).set ("host_rtt_ms", x.hostRttMs)
            .set ("members", encodeList (x.members)).set ("my_join_request_until", x.myJoinRequestUntil)
            .set ("created_at", x.createdAt);
}

static void readRoom (Reader& r, Room& x)
{
    x.id = r.str ("id");
    x.type = r.str ("type");
    x.entryMode = r.optStr ("entry_mode");
    x.title = r.str ("title");
    x.description = r.str ("description");
    x.capacity = r.integer ("capacity");
    x.memberCount = r.integer ("member_count");
    x.hostId = r.str ("host_id");
    x.hostPresent = r.boolean ("host_present");
    x.hostRttMs = r.optInt ("host_rtt_ms");
    r.list ("members", x.members);
    x.myJoinRequestUntil = r.optStr ("my_join_request_until");
    x.createdAt = r.str ("created_at");
}

var encode (const Room& x)
{
    Writer w;
    return writeRoom (w, x).done();
}

bool decode (const var& v, Room& x)
{
    Reader r (v);
    readRoom (r, x);
    return r.ok;
}

var encode (const Connection& c)
{
    juce::Array<var> members;
    for (auto& m : c.members)
        members.add (m);
    return Writer().set ("aoo_host", c.aooHost).set ("aoo_port", c.aooPort).set ("group_name", c.groupName)
                   .set ("group_password", c.groupPassword).set ("aoo_username", c.aooUsername)
                   .set ("aoo_user_password", c.aooUserPassword).set ("members", var (members)).done();
}

bool decode (const var& v, Connection& c)
{
    Reader r (v);
    c.aooHost = r.str ("aoo_host");
    c.aooPort = r.integer ("aoo_port");
    c.groupName = r.str ("group_name");
    c.groupPassword = r.str ("group_password");
    c.aooUsername = r.str ("aoo_username");
    c.aooUserPassword = r.str ("aoo_user_password");
    c.members.clear();
    if (auto* arr = r.get ("members").getArray())
    {
        for (auto& m : *arr)
        {
            if (! m.isString()) return false;
            c.members.add (m.toString());
        }
    }
    else
    {
        return false;
    }
    return r.ok;
}

var encode (const RoomDetail& x)
{
    Writer w;
    writeRoom (w, x);
    return w.set ("invite_code", x.inviteCode).set ("connection", encode (x.connection)).done();
}

bool decode (const var& v, RoomDetail& x)
{
    Reader r (v);
    readRoom (r, x);
    x.inviteCode = r.optStr ("invite_code");
    r.child ("connection", x.connection);
    return r.ok;
}

var encode (const FriendRequest& x)
{
    return Writer().set ("id", x.id).set ("from", encode (x.from)).set ("created_at", x.createdAt).done();
}

bool decode (const var& v, FriendRequest& x)
{
    Reader r (v);
    x.id = r.str ("id");
    r.child ("from", x.from);
    x.createdAt = r.str ("created_at");
    return r.ok;
}

var encode (const Friend& f)
{
    return Writer().set ("user", encode (f.user)).set ("status", f.status).set ("last_seen_at", f.lastSeenAt)
                   .setOpt ("room", f.room).set ("can_invite", f.canInvite).done();
}

bool decode (const var& v, Friend& f)
{
    Reader r (v);
    r.child ("user", f.user);
    f.status = r.str ("status");
    f.lastSeenAt = r.optStr ("last_seen_at");
    r.optChild ("room", f.room);
    f.canInvite = r.boolean ("can_invite");
    return r.ok;
}

var encode (const InviteRoom& x)
{
    return Writer().set ("id", x.id).set ("title", x.title).done();
}

bool decode (const var& v, InviteRoom& x)
{
    Reader r (v);
    x.id = r.str ("id");
    x.title = r.str ("title");
    return r.ok;
}

var encode (const Invite& x)
{
    return Writer().set ("id", x.id).set ("from", encode (x.from)).set ("room", encode (x.room))
                   .set ("created_at", x.createdAt).done();
}

bool decode (const var& v, Invite& x)
{
    Reader r (v);
    x.id = r.str ("id");
    r.child ("from", x.from);
    r.child ("room", x.room);
    x.createdAt = r.str ("created_at");
    return r.ok;
}

var encode (const JoinRequest& x)
{
    return Writer().set ("id", x.id).set ("room_id", x.roomId).set ("user", encode (x.user))
                   .set ("via_invite", x.viaInvite).set ("created_at", x.createdAt).set ("expires_at", x.expiresAt).done();
}

bool decode (const var& v, JoinRequest& x)
{
    Reader r (v);
    x.id = r.str ("id");
    x.roomId = r.str ("room_id");
    r.child ("user", x.user);
    x.viaInvite = r.boolean ("via_invite");
    x.createdAt = r.str ("created_at");
    x.expiresAt = r.str ("expires_at");
    return r.ok;
}

var encode (const Block& x)
{
    return Writer().set ("user", encode (x.user)).set ("created_at", x.createdAt).done();
}

bool decode (const var& v, Block& x)
{
    Reader r (v);
    r.child ("user", x.user);
    x.createdAt = r.str ("created_at");
    return r.ok;
}

var encode (const Config& c)
{
    return Writer().set ("min_build", c.minBuild).set ("latest_build", c.latestBuild)
                   .set ("latency_coefficient", c.latencyCoefficient).set ("discord_url", c.discordUrl).done();
}

bool decode (const var& v, Config& c)
{
    Reader r (v);
    c.minBuild = r.integer ("min_build");
    c.latestBuild = r.integer ("latest_build");
    c.latencyCoefficient = r.number ("latency_coefficient");
    c.discordUrl = r.str ("discord_url");
    return r.ok;
}

var encode (const Hello& h)
{
    return Writer().set ("me", encode (h.me)).setOpt ("current_room", h.currentRoom)
                   .set ("join_requests", encodeList (h.joinRequests)).set ("friends", encodeList (h.friends))
                   .set ("has_unread_notifications", h.hasUnreadNotifications)
                   .set ("pending_invites", encodeList (h.pendingInvites)).set ("config", encode (h.config)).done();
}

bool decode (const var& v, Hello& h)
{
    Reader r (v);
    r.child ("me", h.me);
    r.optChild ("current_room", h.currentRoom);
    r.list ("join_requests", h.joinRequests);
    r.list ("friends", h.friends);
    h.hasUnreadNotifications = r.boolean ("has_unread_notifications");
    r.list ("pending_invites", h.pendingInvites);
    r.child ("config", h.config);
    return r.ok;
}

//==============================================================================
// 요청 본문: 값이 있는 필드만

var encode (const MePatch& p)
{
    Writer w;
    if (p.nickname) w.set ("nickname", *p.nickname);
    if (p.part) w.set ("part", *p.part);
    if (p.friendRequests || p.roomInvites)
    {
        Writer s;
        if (p.friendRequests) s.set ("friend_requests", *p.friendRequests);
        if (p.roomInvites) s.set ("room_invites", *p.roomInvites);
        w.set ("settings", s.done());
    }
    return w.done();
}

bool decode (const var& v, MePatch& p)
{
    Reader r (v);
    if (! r.ok) return false;
    if (r.has ("nickname")) p.nickname = r.str ("nickname");
    if (r.has ("part")) p.part = r.str ("part");
    if (r.has ("settings"))
    {
        Reader s (r.get ("settings"));
        if (s.has ("friend_requests")) p.friendRequests = s.str ("friend_requests");
        if (s.has ("room_invites")) p.roomInvites = s.str ("room_invites");
        r.ok = r.ok && s.ok;
    }
    return r.ok;
}

var encode (const CreateRoom& c)
{
    Writer w;
    w.set ("type", c.type);
    if (c.entryMode) w.set ("entry_mode", *c.entryMode);
    return w.set ("title", c.title).set ("description", c.description).set ("capacity", c.capacity).done();
}

bool decode (const var& v, CreateRoom& c)
{
    Reader r (v);
    c.type = r.str ("type");
    c.entryMode = r.has ("entry_mode") ? r.optStr ("entry_mode") : std::nullopt;
    c.title = r.str ("title");
    c.description = r.has ("description") ? r.str ("description") : String();
    c.capacity = r.integer ("capacity");
    return r.ok;
}

var encode (const RoomPatch& p)
{
    Writer w;
    if (p.title) w.set ("title", *p.title);
    if (p.description) w.set ("description", *p.description);
    if (p.capacity) w.set ("capacity", *p.capacity);
    if (p.type) w.set ("type", *p.type);
    if (p.setEntryMode) w.set ("entry_mode", p.entryMode);
    return w.done();
}

bool decode (const var& v, RoomPatch& p)
{
    Reader r (v);
    if (! r.ok) return false;
    if (r.has ("title")) p.title = r.str ("title");
    if (r.has ("description")) p.description = r.str ("description");
    if (r.has ("capacity")) p.capacity = r.integer ("capacity");
    if (r.has ("type")) p.type = r.str ("type");
    if (r.has ("entry_mode")) { p.setEntryMode = true; p.entryMode = r.optStr ("entry_mode"); }
    return r.ok;
}

//==============================================================================
var encodeError (const ApiError& e)
{
    Writer inner;
    inner.set ("code", errorKey (e.code)).set ("message", e.message);
    if (e.field.isNotEmpty())
        inner.set ("field", e.field);
    return Writer().set ("error", inner.done()).done();
}

ApiError decodeError (int httpStatus, const var& body)
{
    ApiError e;
    e.httpStatus = httpStatus;
    auto inner = body.getProperty ("error", var());
    if (inner.isObject())
    {
        e.code = errorFromKey (inner.getProperty ("code", "").toString());
        e.message = inner.getProperty ("message", "").toString();
        e.field = inner.getProperty ("field", "").toString();
    }
    else
    {
        e.code = ErrorCode::badResponse;
    }
    return e;
}

var obj (std::initializer_list<std::pair<const char*, var>> items)
{
    Writer w;
    for (auto& i : items)
        w.set (i.first, i.second);
    return w.done();
}

//==============================================================================
// UTC 날짜 ↔ 일수 (Howard Hinnant 의 civil_from_days / days_from_civil)

static juce::int64 daysFromCivil (int y, int m, int d)
{
    y -= m <= 2 ? 1 : 0;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (juce::int64) era * 146097 + doe - 719468;
}

String isoTime (juce::int64 ms)
{
    juce::int64 secs = ms >= 0 ? ms / 1000 : (ms - 999) / 1000;
    juce::int64 days = secs >= 0 ? secs / 86400 : (secs - 86399) / 86400;
    const int sod = (int) (secs - days * 86400);

    days += 719468;
    const juce::int64 era = (days >= 0 ? days : days - 146096) / 146097;
    const int doe = (int) (days - era * 146097);
    const int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int mp = (5 * doy + 2) / 153;
    const int d = doy - (153 * mp + 2) / 5 + 1;
    const int m = mp + (mp < 10 ? 3 : -9);
    const int y = (int) (yoe + era * 400) + (m <= 2 ? 1 : 0);

    return String::formatted ("%04d-%02d-%02dT%02d:%02d:%02dZ", y, m, d, sod / 3600, (sod / 60) % 60, sod % 60);
}

juce::int64 parseIsoTime (const String& s)
{
    // "YYYY-MM-DDTHH:MM:SS" + 소수점 초(선택) + "Z"
    if (s.length() < 20 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':')
        return 0;
    const int y = s.substring (0, 4).getIntValue(), mo = s.substring (5, 7).getIntValue(), d = s.substring (8, 10).getIntValue();
    const int h = s.substring (11, 13).getIntValue(), mi = s.substring (14, 16).getIntValue(), se = s.substring (17, 19).getIntValue();
    int ms = 0;
    if (s[19] == '.')
    {
        auto frac = s.substring (20).upToFirstOccurrenceOf ("Z", false, false);
        ms = (frac + "000").substring (0, 3).getIntValue();
    }
    if (! s.endsWithChar ('Z')) return 0;
    return ((daysFromCivil (y, mo, d) * 86400) + h * 3600 + mi * 60 + se) * 1000 + ms;
}

} // namespace meon::core::json
