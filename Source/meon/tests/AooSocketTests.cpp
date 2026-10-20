// AOO 소켓 (엔진 수정 E8·E9): 끊긴 TCP 연결에 써도 SIGPIPE 없이 EPIPE, 거부된 연결은 실패로.
// AOO 의 net_utils.cpp 만 이 시험에 넣는다 (CMakeLists.txt). SIGPIPE 가 나면 시험 실행 파일이 끝나 실패한다
#include <JuceHeader.h>
#include "net_utils.hpp"

namespace meon::core
{

class AooSocketTests : public juce::UnitTest
{
public:
    AooSocketTests() : juce::UnitTest ("AOO sockets (E8 SIGPIPE, E9 connect failure)", "Aoo") {}

    /** 127.0.0.1 의 빈 포트에서 듣는 TCP 소켓 */
    static int listenLocal (int& port)
    {
        const int s = (int) socket (AF_INET, SOCK_STREAM, 0);
        aoo::net::ip_address any (0x7f000001u, 0);
        if (bind (s, (const struct sockaddr*) &any.address, any.length) != 0 || listen (s, 4) != 0) return -1;
        aoo::net::ip_address bound;
        getsockname (s, (struct sockaddr*) &bound.address, &bound.length);
        port = ntohs (((struct sockaddr_in*) &bound.address)->sin_port);
        return s;
    }

    void runTest() override
    {
        using namespace aoo::net;
       #if JUCE_WINDOWS
        WSADATA wsa;
        WSAStartup (MAKEWORD (2, 2), &wsa);
       #endif

        beginTest ("E9: connecting to a refused port fails (SO_ERROR after select), an open port succeeds");
        {
            int port = 0;
            const int l = listenLocal (port);
            expect (l >= 0 && port > 0);
            const int ok = (int) socket (AF_INET, SOCK_STREAM, 0);
            expectEquals (socket_connect (ok, ip_address (0x7f000001u, port), 2.0f), 0, "듣는 포트: 성공");
            socket_close (ok);
            socket_close (l);   // 이제 아무도 듣지 않는다

            const int refused = (int) socket (AF_INET, SOCK_STREAM, 0);
            const int r = socket_connect (refused, ip_address (0x7f000001u, port), 5.0f);   // Windows 는 거부를 1초쯤 뒤에 알린다
            const int err = socket_errno();
            expect (r < 0, "거부된 연결은 실패 (예전에는 쓰기 가능만 보고 성공으로 봤다)");
           #if JUCE_WINDOWS
            expectEquals (err, (int) WSAECONNREFUSED);
           #else
            expectEquals (err, (int) ECONNREFUSED);
           #endif
            socket_close (refused);
        }

       #if ! JUCE_WINDOWS
        beginTest ("E8: writing to a connection the peer reset returns an error (EPIPE), no SIGPIPE");
        {
            int port = 0;
            const int l = listenLocal (port);
            const int c = (int) socket (AF_INET, SOCK_STREAM, 0);
            expectEquals (socket_set_nosigpipe (c), 0);
            expectEquals (socket_connect (c, ip_address (0x7f000001u, port), 2.0f), 0);
            const int a = (int) accept (l, nullptr, nullptr);
            struct linger lg { 1, 0 };
            setsockopt (a, SOL_SOCKET, SO_LINGER, &lg, sizeof (lg));
            socket_close (a);   // RST: 상대가 연결을 끊었다
            juce::Thread::sleep (50);
            // 첫 쓰기는 ECONNRESET 일 수 있다 (신호 없음). 그다음 쓰기가 EPIPE 이고, 막지 않았으면 여기서 SIGPIPE 로 끝난다
            bool epipe = false;
            for (int i = 0; i < 20 && ! epipe; ++i)
            {
                if (::send (c, "x", 1, socket_send_flags) < 0 && errno == EPIPE) epipe = true;
                juce::Thread::sleep (5);
            }
            expect (epipe, "끊긴 연결에 쓰기: EPIPE 를 돌려받고 프로세스는 그대로");
            socket_close (c);
            socket_close (l);
        }
       #endif
    }
};

static AooSocketTests aooSocketTests;

} // namespace meon::core
