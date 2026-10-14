#include "SystemCerts.h"

#if JUCE_MAC
 #include <Security/Security.h>
#endif

namespace meon::core::certs
{

#if JUCE_MAC
static std::string toPem (const void* der, size_t size)
{
    const auto b64 = juce::Base64::toBase64 (der, size);
    std::string pem = "-----BEGIN CERTIFICATE-----\n";
    for (int i = 0; i < b64.length(); i += 64)
        pem += b64.substring (i, i + 64).toStdString() + "\n";
    pem += "-----END CERTIFICATE-----\n";
    return pem;
}

struct MacRoots
{
    std::string pem;
    int count = 0;

    MacRoots()
    {
        // 시스템이 믿는 루트 인증서 (키체인 "시스템 루트"). 시스템 업데이트로 바뀌면 다음 실행부터 반영된다
        CFArrayRef anchors = nullptr;
        if (SecTrustCopyAnchorCertificates (&anchors) != errSecSuccess || anchors == nullptr)
            return;
        for (CFIndex i = 0; i < CFArrayGetCount (anchors); ++i)
        {
            auto cert = (SecCertificateRef) CFArrayGetValueAtIndex (anchors, i);
            if (CFDataRef der = SecCertificateCopyData (cert))
            {
                pem += toPem (CFDataGetBytePtr (der), (size_t) CFDataGetLength (der));
                ++count;
                CFRelease (der);
            }
        }
        CFRelease (anchors);
    }
};

static const MacRoots& macRoots()
{
    static const MacRoots roots;   // 처음 쓸 때 한 번 (스레드 안전)
    return roots;
}
#endif

std::string caFileForTls()
{
   #if JUCE_WINDOWS
    return "SYSTEM";
   #elif JUCE_MAC
    return macRoots().pem;
   #else
    for (auto* path : { "/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/cert.pem" })
        if (juce::File (path).existsAsFile())
            return path;
    return {};
   #endif
}

int systemRootCount()
{
   #if JUCE_MAC
    return macRoots().count;
   #else
    return -1;
   #endif
}

} // namespace meon::core::certs
