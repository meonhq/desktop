// MEON core - 합주 채팅 (버그 #8, REMAKE_PLAN 6.2). 멤버끼리 직접(엔진 채팅) + 길이 제한 + 전달 확인.
//   보낼 때: tags "id:<ID>" 를 싣는다. 받은 쪽은 화면에 보이고 "ack:<ID>" (본문 없음)를 돌려보낸다.
//   3초 안에 그 순간의 멤버(나 제외) 전원의 확인이 없으면 실패 표시 → 다시 보내기.
//   받을 멤버(나 제외, 가짜 각본 멤버 제외)가 없으면 보내지 않고 내 목록에만 보인다 (localOnly).
//   실제 멤버가 있으면 연결 중·연결 실패여도 보내고 확인을 기다린다. 나중에 들어온 사람에게는 보이지 않는다.
//   가짜 기간에만: tags "profile" 로 닉네임·파트를 주고받는다 (서버가 멤버 정보를 주기 전까지).
// 시각·보내기는 인자로 받는다 (테스트).
#pragma once

#include "Engine.h"
#include "../util/Scheduler.h"

namespace meon::core
{

class ChatChannel
{
public:
    static constexpr int maxBytes = 1000;
    static constexpr int ackTimeoutMs = 3000;

    struct Message
    {
        enum class Kind { mine, other, system };
        Kind kind = Kind::other;
        juce::String id, fromUser, text;
        juce::int64 time = 0;
        enum class Delivery { none, sending, delivered, failed } delivery = Delivery::none;
        juce::StringArray waitingFor;   // 내 메시지: 아직 확인이 없는 멤버
        int attempt = 0;
    };

    enum class SendResult { sent, empty, tooLong, localOnly, failed };

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void chatChanged() {}
        /** 목록에 메시지가 새로 생김 (내 첫 전송·받은 새 메시지·시스템 줄). 다시 보내기·중복 수신·전달 상태 변화에는 부르지 않는다 */
        virtual void chatMessageAdded (const Message& m) { juce::ignoreUnused (m); }
        /** 가짜 기간 프로필 메시지 */
        virtual void chatProfile (const juce::String& user, const juce::var& profile) { juce::ignoreUnused (user, profile); }
        /** 실패 표시가 생김 (로그 chatAckMissing) */
        virtual void chatAckMissing (const juce::String& id, const juce::StringArray& missing) { juce::ignoreUnused (id, missing); }
    };

    /** send: 엔진으로 보낸다 (실패하면 false) */
    ChatChannel (Scheduler& s, std::function<bool (const EngineChat&)> sendFn) : scheduler (s), send (std::move (sendFn)) {}
    ~ChatChannel() { *alive = false; }

    void addListener (Listener* l)    { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    /** 방에 들어갈 때 */
    void begin (const juce::String& group, const juce::String& me) { groupName = group; myUser = me; messages.clear(); unread = 0; changed(); }
    void end() { groupName = {}; messages.clear(); changed(); }

    /** 지금 방 멤버 (나 제외, 가짜 각본 멤버 제외) — 확인을 기다릴 대상. 비어 있으면 내 목록에만 */
    void setOthers (const juce::StringArray& users) { others = users; }

    /** UTF-8 바이트 수 */
    static int byteLength (const juce::String& s) { return (int) s.getNumBytesAsUTF8(); }

    SendResult sendText (const juce::String& raw)
    {
        const auto text = raw.trim();
        if (text.isEmpty()) return SendResult::empty;
        if (byteLength (text) > maxBytes) return SendResult::tooLong;

        Message m;
        m.kind = Message::Kind::mine;
        m.id = juce::Uuid().toString().substring (0, 12);
        m.fromUser = myUser;
        m.text = text;
        m.time = scheduler.nowMs();
        if (others.isEmpty())
        {
            // 받을 사람이 없다: 엔진으로 보내지 않고 내 목록에만 (확인·실패 표시 없음)
            messages.push_back (m);
            added (messages.back());
            changed();
            return SendResult::localOnly;
        }
        m.delivery = Message::Delivery::sending;
        m.waitingFor = others;
        messages.push_back (m);
        added (messages.back());
        transmit (messages.back());
        changed();
        return messages.back().delivery == Message::Delivery::failed ? SendResult::failed : SendResult::sent;
    }

    /** 실패한 내 메시지를 같은 ID 로 다시 보낸다 (지금 멤버 기준) */
    void resend (const juce::String& id)
    {
        for (auto& m : messages)
        {
            if (m.id != id || m.kind != Message::Kind::mine) continue;
            m.delivery = Message::Delivery::sending;
            m.waitingFor = others;
            transmit (m);
            changed();
            return;
        }
    }

    /** 시스템 줄. aboutUser 가 있으면 화면이 그 사람의 지금 이름으로 다시 쓸 수 있다 (가짜 기간엔 프로필이 늦게 온다) */
    void addSystem (const juce::String& text, const juce::String& aboutUser = {})
    {
        Message m;
        m.kind = Message::Kind::system;
        m.text = text;
        m.fromUser = aboutUser;
        m.time = scheduler.nowMs();
        messages.push_back (m);
        added (messages.back());
        changed();
    }

    /** 가짜 기간: 내 프로필을 보낸다 */
    void sendProfile (const juce::var& profile)
    {
        if (groupName.isEmpty()) return;
        send ({ groupName, myUser, "profile", juce::JSON::toString (profile, true) });
    }

    /** 엔진에서 받은 채팅 (멤버가 아닌 사람 것은 부르기 전에 걸러야 한다) */
    void received (const EngineChat& c)
    {
        if (c.group != groupName || c.from == myUser) return;

        if (c.tags == "profile")
        {
            const auto v = juce::JSON::parse (c.message);
            listeners.call ([&] (Listener& l) { l.chatProfile (c.from, v); });
            return;
        }
        if (c.tags.startsWith ("ack:"))
        {
            const auto id = c.tags.fromFirstOccurrenceOf ("ack:", false, false);
            for (auto& m : messages)
            {
                if (m.id != id || m.kind != Message::Kind::mine) continue;
                m.waitingFor.removeString (c.from);
                if (m.waitingFor.isEmpty() && m.delivery != Message::Delivery::delivered)
                {
                    m.delivery = Message::Delivery::delivered;
                    changed();
                }
            }
            return;
        }
        if (c.message.trim().isEmpty()) return;

        const auto id = c.tags.startsWith ("id:") ? c.tags.fromFirstOccurrenceOf ("id:", false, false) : juce::String();
        if (id.isNotEmpty())
        {
            send ({ groupName, myUser, "ack:" + id, {} });
            for (auto& m : messages)
                if (m.id == id && m.fromUser == c.from) return;   // 다시 보내기로 온 같은 메시지
        }

        Message m;
        m.kind = Message::Kind::other;
        m.id = id;
        m.fromUser = c.from;
        m.text = c.message.substring (0, juce::jmin (c.message.length(), maxBytes));
        m.time = scheduler.nowMs();
        messages.push_back (m);
        ++unread;
        added (messages.back());
        changed();
    }

    const std::vector<Message>& getMessages() const { return messages; }
    const juce::String& group() const { return groupName; }   // 개발용 미리 보기가 받은 메시지를 흉내 낼 때
    int getUnread() const { return unread; }
    void markRead() { unread = 0; }

private:
    Scheduler& scheduler;
    std::function<bool (const EngineChat&)> send;
    juce::ListenerList<Listener> listeners;
    juce::String groupName, myUser;
    juce::StringArray others;
    std::vector<Message> messages;
    int unread = 0;
    std::shared_ptr<bool> alive { std::make_shared<bool> (true) };

    void changed() { listeners.call ([] (Listener& l) { l.chatChanged(); }); }
    void added (Message m) { listeners.call ([&m] (Listener& l) { l.chatMessageAdded (m); }); }   // 복사본: 듣는 쪽이 목록을 바꿔도 안전

    void transmit (Message& m)
    {
        if (! send ({ groupName, myUser, "id:" + m.id, m.text }))
        {
            m.delivery = Message::Delivery::failed;
            return;
        }
        std::weak_ptr<bool> weak = alive;
        const auto id = m.id;
        const int attempt = ++m.attempt;
        scheduler.post (ackTimeoutMs, [this, weak, id, attempt]
        {
            if (weak.expired()) return;
            for (auto& x : messages)
            {
                if (x.id != id || x.kind != Message::Kind::mine || x.attempt != attempt || x.delivery != Message::Delivery::sending) continue;
                if (x.waitingFor.isEmpty()) { x.delivery = Message::Delivery::delivered; changed(); return; }
                x.delivery = Message::Delivery::failed;
                const auto missing = x.waitingFor;
                listeners.call ([&] (Listener& l) { l.chatAckMissing (id, missing); });
                changed();
                return;
            }
        });
    }
};

} // namespace meon::core
