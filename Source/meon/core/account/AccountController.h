// MEON core - 내 계정·프로필·차단 (API 4·7.6·8장): 프로필 보기, 닉네임·파트·개인정보 설정 바꾸기, 비밀번호·이메일 변경,
// 친구 삭제, 차단·해제, 로그아웃, 탈퇴. 결과는 store 에 반영하고 화면에는 Result 로 돌려준다.
//
// 잠정 (API 질문 대기): 사용자 결정으로 이메일 변경·탈퇴 화면에는 비밀번호 칸이 없다. API 4.4·4.5 는 비밀번호를 받으므로
// 지금은 빈 비밀번호로 보낸다 (가짜 서버는 받아 준다). 서버 쪽 방식이 정해지면 여기만 바꾼다.
#pragma once

#include "../api/MeonApi.h"
#include "../state/AppStore.h"
#include "../auth/AuthController.h"

namespace meon::core
{

class AccountController
{
public:
    AccountController (MeonApi&, AppStore&, AuthController&);
    ~AccountController();

    void loadProfile (const juce::String& userId, Callback<UserProfile>);

    void setNickname (const juce::String&, Callback<Me> = {});
    void setPart (const juce::String&, Callback<Me> = {});
    /** "everyone" | "none" */
    void setFriendRequests (const juce::String&, Callback<Me> = {});
    /** "friends" | "none" */
    void setRoomInvites (const juce::String&, Callback<Me> = {});

    void changePassword (const juce::String& current, const juce::String& next, Callback<Done>);
    void requestEmailChange (const juce::String& newEmail, Callback<Done>);
    void resendEmailChange (Callback<Done>);
    void verifyEmailChange (const juce::String& code, Callback<Me>);

    void removeFriend (const juce::String& userId, Callback<Done> = {});
    void block (const juce::String& userId, Callback<Done> = {});
    void unblock (const juce::String& userId, Callback<Done> = {});
    void listBlocks (Callback<std::vector<Block>>);

    void logout();
    void deleteAccount (Callback<Done>);

private:
    MeonApi& api;
    AppStore& store;
    AuthController& auth;
    juce::String pendingEmail;
    std::shared_ptr<bool> alive { std::make_shared<bool> (true) };

    void patch (const MePatch&, Callback<Me>);
};

} // namespace meon::core
