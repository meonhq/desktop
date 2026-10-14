// MEON core - 내 계정·프로필·차단 (API 4·7.6·8장): 프로필 보기, 닉네임·파트·개인정보 설정 바꾸기, 비밀번호·이메일 변경,
// 친구 삭제, 차단·해제, 로그아웃, 탈퇴. 결과는 store 에 반영하고 화면에는 Result 로 돌려준다.
//
// 이메일 변경은 지금 비밀번호를 받는다 (API 4.4. 인증을 마친 계정). 탈퇴는 비밀번호 없이 세션만으로 (API 4.5, v0.5).
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
    /** 새 주소로 코드 보내기. password 는 지금 비밀번호 (다시 보내기에 쓰려고 이 흐름이 끝날 때까지만 기억한다) */
    void requestEmailChange (const juce::String& newEmail, const juce::String& password, Callback<Done>);
    void resendEmailChange (Callback<Done>);
    /** 이메일 변경 취소 (기억한 주소·비밀번호를 지운다) */
    void cancelEmailChange();
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
    juce::String pendingEmail, pendingPassword;
    std::shared_ptr<bool> alive { std::make_shared<bool> (true) };

    void patch (const MePatch&, Callback<Me>);
};

} // namespace meon::core
