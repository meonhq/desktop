#if defined (__linux__) || defined (__FreeBSD__)
 #include <sys/stat.h>
#endif

#if defined (__APPLE__)
 #include <Security/Security.h>
#elif defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <wincred.h>
 #if defined (_MSC_VER)
  #pragma comment (lib, "advapi32.lib")
 #endif
#endif

// 시스템 헤더를 JuceHeader(using namespace juce) 보다 먼저 넣는다 (그래서 JUCE_MAC 대신 컴파일러 매크로로 가른다): macOS MacTypes.h 의 Point 가 juce::Point 와 겹친다
#include "TokenStore.h"

namespace meon::core
{

using juce::String;

namespace
{
#if JUCE_MAC
    class KeychainTokenStore : public TokenStore
    {
    public:
        explicit KeychainTokenStore (const String& n) : account (n) {}

        String load() override
        {
            auto query = baseQuery();
            CFDictionarySetValue (query, kSecReturnData, kCFBooleanTrue);
            CFDictionarySetValue (query, kSecMatchLimit, kSecMatchLimitOne);
            CFTypeRef result = nullptr;
            const auto status = SecItemCopyMatching (query, &result);
            CFRelease (query);
            error = (status == errSecSuccess || status == errSecItemNotFound) ? String() : juce::String::fromUTF8 ("키체인 읽기 오류 ") + String ((int) status);
            if (status != errSecSuccess || result == nullptr) return {};
            auto data = (CFDataRef) result;
            const String token = String::fromUTF8 ((const char*) CFDataGetBytePtr (data), (int) CFDataGetLength (data));
            CFRelease (result);
            return token;
        }

        bool save (const String& token) override
        {
            const auto utf8 = token.toRawUTF8();
            auto data = CFDataCreate (nullptr, (const UInt8*) utf8, (CFIndex) strlen (utf8));

            auto query = baseQuery();
            auto update = CFDictionaryCreateMutable (nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            CFDictionarySetValue (update, kSecValueData, data);
            auto status = SecItemUpdate (query, update);
            CFRelease (update);

            if (status == errSecItemNotFound)
            {
                CFDictionarySetValue (query, kSecValueData, data);
                CFDictionarySetValue (query, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlock);
                status = SecItemAdd (query, nullptr);
            }
            CFRelease (query);
            CFRelease (data);
            error = status == errSecSuccess ? String() : juce::String::fromUTF8 ("키체인 저장 오류 ") + String ((int) status);
            return status == errSecSuccess;
        }

        void clear() override
        {
            auto query = baseQuery();
            SecItemDelete (query);
            CFRelease (query);
        }

    private:
        String account;

        CFMutableDictionaryRef baseQuery() const
        {
            auto q = CFDictionaryCreateMutable (nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            auto service = CFStringCreateWithCString (nullptr, "com.meon.MEON", kCFStringEncodingUTF8);
            auto acc = CFStringCreateWithCString (nullptr, account.toRawUTF8(), kCFStringEncodingUTF8);
            CFDictionarySetValue (q, kSecClass, kSecClassGenericPassword);
            CFDictionarySetValue (q, kSecAttrService, service);
            CFDictionarySetValue (q, kSecAttrAccount, acc);
            CFRelease (service);
            CFRelease (acc);
            return q;
        }
    };
#elif JUCE_WINDOWS
    class CredentialTokenStore : public TokenStore
    {
    public:
        explicit CredentialTokenStore (const String& n) : target ("MEON/" + n) {}

        String load() override
        {
            PCREDENTIALW cred = nullptr;
            if (! CredReadW (target.toWideCharPointer(), CRED_TYPE_GENERIC, 0, &cred))
            {
                const auto e = GetLastError();
                error = e == ERROR_NOT_FOUND ? String() : juce::String::fromUTF8 ("자격 증명 읽기 오류 ") + String ((int) e);
                return {};
            }
            error = {};
            const String token = String::fromUTF8 ((const char*) cred->CredentialBlob, (int) cred->CredentialBlobSize);
            CredFree (cred);
            return token;
        }

        bool save (const String& token) override
        {
            juce::MemoryBlock blob (token.toRawUTF8(), token.getNumBytesAsUTF8());
            CREDENTIALW cred {};
            cred.Type = CRED_TYPE_GENERIC;
            cred.TargetName = const_cast<LPWSTR> (target.toWideCharPointer());
            cred.CredentialBlobSize = (DWORD) blob.getSize();
            cred.CredentialBlob = (LPBYTE) blob.getData();
            cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
            cred.UserName = const_cast<LPWSTR> (L"MEON");
            if (CredWriteW (&cred, 0) != FALSE) { error = {}; return true; }
            const auto e = GetLastError();
            error = juce::String::fromUTF8 ("자격 증명 저장 오류 ") + String ((int) e);
            return false;
        }

        void clear() override
        {
            CredDeleteW (target.toWideCharPointer(), CRED_TYPE_GENERIC, 0);
        }

    private:
        String target;
    };
#endif

    /** Linux 등: 사용자 데이터 폴더의 파일 */
    class FileTokenStore : public TokenStore
    {
    public:
        explicit FileTokenStore (const String& n)
            : file (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("MEON").getChildFile (n + ".token")) {}

        String load() override { return file.existsAsFile() ? file.loadFileAsString().trim() : String(); }

        bool save (const String& token) override
        {
            file.getParentDirectory().createDirectory();
            const bool ok = file.replaceWithText (token);
            error = ok ? String() : juce::String::fromUTF8 ("파일 쓰기 실패 ") + file.getFullPathName();
           #if JUCE_LINUX || JUCE_BSD
            if (ok) ::chmod (file.getFullPathName().toRawUTF8(), 0600);
           #endif
            return ok;
        }

        void clear() override { file.deleteFile(); }

    private:
        juce::File file;
    };
}

std::unique_ptr<TokenStore> TokenStore::createDefault (const String& name)
{
   #if JUCE_MAC
    return std::make_unique<KeychainTokenStore> (name);
   #elif JUCE_WINDOWS
    return std::make_unique<CredentialTokenStore> (name);
   #else
    return std::make_unique<FileTokenStore> (name);
   #endif
}

} // namespace meon::core
