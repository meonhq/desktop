#include "../MeonRtGate.h"

#include <chrono>
#include <thread>
#include <vector>

namespace meon
{

// E10 관문: 오디오 스레드는 기다리지 않고, 쓰는 쪽은 오디오 스레드가 나간 뒤에 바꾼다
class RtGateTests : public juce::UnitTest
{
public:
    RtGateTests() : juce::UnitTest ("RtGate", "Engine") {}

    void runTest() override
    {
        beginTest ("Reader enters when nobody writes");
        {
            RtGate g;
            {
                RtGate::Reader r (g);
                expect (r.entered());
            }
            expectEquals ((int) g.skippedBlocks(), 0);
        }

        beginTest ("Reader skips while a writer holds the gate, and enters again after release");
        {
            RtGate g;
            {
                RtGate::Writer w (g);
                RtGate::Reader r (g);
                expect (! r.entered());
                w.release();
                w.release();   // 두 번 불러도 된다
                RtGate::Reader r2 (g);
                expect (r2.entered());
            }
            expectEquals ((int) g.skippedBlocks(), 1);
            RtGate::Reader r3 (g);
            expect (r3.entered());
        }

        beginTest ("Nested writers keep the gate closed until the last one leaves");
        {
            RtGate g;
            RtGate::Writer outer (g);
            {
                RtGate::Writer inner (g);
                expect (! RtGate::Reader (g).entered());
            }
            expect (! RtGate::Reader (g).entered());
            outer.release();
            expect (RtGate::Reader (g).entered());
        }

        beginTest ("Writer waits until the reader has finished its block");
        {
            RtGate g;
            std::atomic<bool> readerIn { false }, readerDone { false };
            std::thread audio ([&]
            {
                RtGate::Reader r (g);
                readerIn = r.entered();
                std::this_thread::sleep_for (std::chrono::milliseconds (60));   // 긴 블록
                readerDone = true;
            });
            while (! readerIn.load())
                std::this_thread::yield();

            const auto t0 = std::chrono::steady_clock::now();
            bool doneWhenWriterGotIn = false;
            {
                RtGate::Writer w (g);
                doneWhenWriterGotIn = readerDone.load();
            }
            const auto waitedMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
            audio.join();
            expect (doneWhenWriterGotIn, "쓰는 쪽이 오디오 스레드가 읽는 도중에 들어왔다");
            expectGreaterThan (waitedMs, 30.0);
        }

        beginTest ("Writer can come from the audio thread itself outside its block");
        {
            RtGate g;
            { RtGate::Reader r (g); expect (r.entered()); }
            RtGate::Writer w (g);   // inside == 0 이므로 바로 돌아온다
            expect (! RtGate::Reader (g).entered());
        }

        beginTest ("Stress: the reader never sees a half-written or freed table");
        {
            // 쓰는 쪽은 표를 새로 만들어 바꾸고 옛 표를 지운다 (사람 추가·삭제처럼). 읽는 쪽은 표 전체가 한 세대인지 본다.
            RtGate g;
            std::vector<int>* table = new std::vector<int> (64, 0);
            std::atomic<bool> stop { false };
            std::atomic<int> torn { 0 }, entered { 0 }, skipped { 0 };

            std::thread audio ([&]
            {
                while (! stop.load())
                {
                    RtGate::Reader r (g);
                    if (! r.entered()) { ++skipped; continue; }
                    ++entered;
                    const auto& t = *table;
                    const int first = t.front();
                    for (int v : t)
                        if (v != first) { ++torn; break; }
                }
            });

            const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds (1500);
            int gen = 0;
            while (std::chrono::steady_clock::now() < until)
            {
                ++gen;
                RtGate::Writer w (g);
                auto* old = table;
                table = new std::vector<int> ((size_t) (16 + gen % 80), gen);
                for (auto& v : *old) v = -1;   // 지우기 전에 망가뜨린다: 읽는 중이었다면 torn 이 오른다
                delete old;
                // 한 칸씩 고쳐 쓰기 (형식 바꾸기처럼)
                for (auto& v : *table) v = gen + 1;
            }
            stop = true;
            audio.join();
            delete table;

            logMessage ("RtGate stress: writes " + juce::String (gen) + ", entered " + juce::String (entered.load())
                        + ", skipped " + juce::String (skipped.load()));
            expectEquals (torn.load(), 0);
            expectGreaterThan (entered.load(), 0);
            expectGreaterThan (gen, 100);
        }
    }
};

static RtGateTests rtGateTests;

} // namespace meon
