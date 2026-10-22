# E10 ThreadSanitizer 경고 목록

`scripts/stall/run-jam.sh` 를 ThreadSanitizer 빌드(`-fsanitize=thread -O1`)로 돌린 결과 (Linux, 각 10분, 서버 따로). 같은 시험을 main(4385297, E10 전)과 E10 으로 했다.
자리는 두 스택의 맨 위 우리 코드 함수로 묶었다 (줄 번호는 E10 으로 밀려서 뺐다). 숫자는 경고 건수.

| | main 2인 | E10 2인 | main 들고남 | E10 들고남 |
|---|---|---|---|---|
| 경고 | 133 | 141 | 6949 | 8360 |
| E10 관련 (관문 안 오디오 구간 ↔ 관문을 건 쓰는 쪽, 또는 관문) | – | 0 | – | 0 |
| 앱 크래시 (SEGV) | 0 | 0 | A (B 미터, 아래) | A·C 2번 (B 미터, 아래) |

**E10 관련 경고는 0 건이다.** 아래는 모두 E10 전부터 있던 것 (E10 이 없앤 A 제외). 고치지 않았다 (따로 승인 필요).


### A. 오디오 ↔ 받는 형식 바꾸기 (E10 이 막음)

| 종류 | 한쪽 | 다른 쪽 | main 2인 | E10 2인 | main 들고남 | E10 들고남 |
|---|---|---|---|---|---|---|
| heap-use-after-free | `SonobusAudioProcessor::handleSinkEvents (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::processBlock (Source/SonobusPluginProcessor.cpp)` | 1 |  | 13 |  |

### B. 화면 미터 ↔ 사람 지우기·형식 바꾸기

| 종류 | 한쪽 | 다른 쪽 | main 2인 | E10 2인 | main 들고남 | E10 들고남 |
|---|---|---|---|---|---|---|
| data race | `SonobusAudioProcessor::handleSinkEvents (Source/SonobusPluginProcessor.cpp)` | `meon::core::EngineLink::peerLevel (Source/meon/core/jam/EngineLink.cpp)` | 1 | 1 | 5 | 12 |
| heap-use-after-free | `SonobusAudioProcessor::getRemotePeerRecvMeterSource (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::updateRemotePeerUserFormat (Source/SonobusPluginProcessor.cpp)` |  |  |  | 2 |

### C. 오디오 ↔ 읽기 잠금만 잡고 바꾸는 값 (볼륨·패닝·압축기·EQ·뮤트·피어 상태)

| 종류 | 한쪽 | 다른 쪽 | main 2인 | E10 2인 | main 들고남 | E10 들고남 |
|---|---|---|---|---|---|---|
| data race | `SonobusAudioProcessor::processBlock (Source/SonobusPluginProcessor.cpp)` | `meon::core::EngineLink::applyDefaults (Source/meon/core/jam/EngineLink.cpp)` | 8 | 8 | 476 | 480 |
| data race | `SonobusAudioProcessor::handleSinkEvents (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::processBlock (Source/SonobusPluginProcessor.cpp)` | 6 | 2 | 677 | 189 |
| data race | `SonoAudio::ChannelGroup::processBlock (Source/ChannelGroup.cpp)` | `SonobusAudioProcessor::setInputExpanderParams (Source/SonobusPluginProcessor.cpp)` | 6 | 6 | 338 | 337 |
| data race | `SonoAudio::ChannelGroup::processBlock (Source/ChannelGroup.cpp)` | `SonobusAudioProcessor::setInputEqParams (Source/SonobusPluginProcessor.cpp)` | 6 | 6 | 329 | 334 |
| data race | `SonoAudio::ChannelGroup::processBlock (Source/ChannelGroup.cpp)` | `SonobusAudioProcessor::setInputLimiterParams (Source/SonobusPluginProcessor.cpp)` | 6 | 5 | 312 | 307 |
| data race | `SonoAudio::ChannelGroup::processBlock (Source/ChannelGroup.cpp)` | `SonoAudio::ChannelGroupParams::operator= (Source/ChannelGroup.h)` | 8 | 8 | 115 | 428 |
| data race | `SonoAudio::ChannelGroup::commitEqParams (Source/ChannelGroup.cpp)` | `SonobusAudioProcessor::setInputEqParams (Source/SonobusPluginProcessor.cpp)` |  |  | 250 | 215 |
| data race | `SonoAudio::ChannelGroup::processBlock (Source/ChannelGroup.cpp)` | `SonobusAudioProcessor::setInputCompressorParams (Source/SonobusPluginProcessor.cpp)` | 4 | 4 | 227 | 226 |
| data race | `SonoAudio::ChannelGroup::processMonitor (Source/ChannelGroup.cpp)` | `SonobusAudioProcessor::setInputMonitorPan (Source/SonobusPluginProcessor.cpp)` | 4 | 4 | 197 | 206 |
| data race | `SonoAudio::ChannelGroup::processMonitor (Source/ChannelGroup.cpp)` | `SonobusAudioProcessor::setInputMonitor (Source/SonobusPluginProcessor.cpp)` | 4 | 4 | 189 | 196 |
| data race | `SonobusAudioProcessor::processBlock (Source/SonobusPluginProcessor.cpp)` | `stats::RunCumulantor1D::reset (Source/RunCumulantor.h)` | 2 | 5 | 66 | 316 |
| data race | `SonoAudio::ChannelGroupParams::operator= (Source/ChannelGroup.h)` | `SonobusAudioProcessor::processBlock (Source/SonobusPluginProcessor.cpp)` | 4 | 4 | 68 | 228 |
| data race | `SonobusAudioProcessor::processBlock (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::setInputGroupChannelDestStartAndCount (Source/SonobusPluginProcessor.cpp)` | 2 | 2 | 140 | 137 |
| data race | `SonoAudio::ChannelGroup::processBlock (Source/ChannelGroup.cpp)` | `SonobusAudioProcessor::setInputGroupChannelStartAndCount (Source/SonobusPluginProcessor.cpp)` | 2 | 2 | 120 | 120 |
| data race | `SonoAudio::ChannelGroup::processBlock (Source/ChannelGroup.cpp)` | `SonobusAudioProcessor::setInputGroupGain (Source/SonobusPluginProcessor.cpp)` | 2 | 2 | 118 | 118 |
| data race | `SonobusAudioProcessor::processBlock (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::setInputGroupCount (Source/SonobusPluginProcessor.cpp)` | 2 | 2 | 113 | 118 |
| data race | `SonobusAudioProcessor::processBlock (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::setInputCompressorParams (Source/SonobusPluginProcessor.cpp)` | 2 | 2 | 103 | 109 |
| data race | `SonobusAudioProcessor::doReceiveData (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::processBlock (Source/SonobusPluginProcessor.cpp)` |  | 3 | 63 | 124 |
| data race | `SonoAudio::ChannelGroup::commitLimiterParams (Source/ChannelGroup.cpp)` | `SonobusAudioProcessor::setInputLimiterParams (Source/SonobusPluginProcessor.cpp)` |  | 3 | 78 | 86 |
| data race | `SonobusAudioProcessor::processBlock (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::updateSafetyMuting (Source/SonobusPluginProcessor.cpp)` |  | 1 | 14 | 115 |
| data race | `SonobusAudioProcessor::processBlock (Source/SonobusPluginProcessor.cpp)` | `meon::core::EngineLink::applyPeerDefaults (Source/meon/core/jam/EngineLink.cpp)` | 2 | 1 | 19 | 106 |
| data race | `SonobusAudioProcessor::processBlock (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::restoreLayoutFormatForPeer (Source/SonobusPluginProcessor.cpp)` | 2 | 2 | 30 | 89 |
| data race | `SonobusAudioProcessor::processBlock (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::setRemotePeerLevelGain (Source/SonobusPluginProcessor.cpp)` | 1 |  | 17 | 98 |
| data race | `SonobusAudioProcessor::handleSourceEvents (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::processBlock (Source/SonobusPluginProcessor.cpp)` |  | 2 | 32 | 66 |
| data race | `SonoAudio::ChannelGroup::commitCompressorParams (Source/ChannelGroup.cpp)` | `SonobusAudioProcessor::setInputCompressorParams (Source/SonobusPluginProcessor.cpp)` |  |  | 50 | 27 |
| data race | `SonobusAudioProcessor::adjustRemoteSendMatrix (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::processBlock (Source/SonobusPluginProcessor.cpp)` |  |  | 24 | 29 |
| data race | `SonoAudio::ChannelGroup::processPan (Source/ChannelGroup.cpp)` | `SonoAudio::ChannelGroupParams::operator= (Source/ChannelGroup.h)` |  |  | 6 | 6 |
| data race | `SonoAudio::ChannelGroup::commitExpanderParams (Source/ChannelGroup.cpp)` | `SonobusAudioProcessor::setInputExpanderParams (Source/SonobusPluginProcessor.cpp)` |  |  | 3 | 9 |
| data race | `SonobusAudioProcessor::disconnectRemotePeer (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::processBlock (Source/SonobusPluginProcessor.cpp)` |  |  | 2 | 4 |
| data race | `SonoAudio::ChannelGroup::processBlock (Source/ChannelGroup.cpp)` | `SonobusAudioProcessor::setInputGroupMuted (Source/SonobusPluginProcessor.cpp)` |  |  | 2 | 2 |
| data race | `SonoAudio::ChannelGroup::processPan (Source/ChannelGroup.cpp)` | `SonobusAudioProcessor::setRemotePeerChannelPan (Source/SonobusPluginProcessor.cpp)` |  |  |  | 1 |

### D. 오디오 밖 스레드끼리 (통계·지연 정보·보내기·받기·이벤트)

| 종류 | 한쪽 | 다른 쪽 | main 2인 | E10 2인 | main 들고남 | E10 들고남 |
|---|---|---|---|---|---|---|
| data race | `endpoint_send (Source/SonobusPluginProcessor.cpp)` | `endpoint_send (Source/SonobusPluginProcessor.cpp)` | 6 | 8 | 549 | 521 |
| data race | `SonobusAudioProcessor::getRemotePeerLatencyInfo (Source/SonobusPluginProcessor.cpp)` | `push_aggregate (Source/RunningCumulant.c)` | 6 | 7 | 224 | 359 |
| data race | `push_aggregate (Source/RunningCumulant.c)` | `stats::RunCumulantor1D::reset (Source/RunCumulantor.h)` | 4 | 7 | 104 | 430 |
| data race | `SonobusAudioProcessor::doSendData (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::getRemotePeerPacketsSent (Source/SonobusPluginProcessor.cpp)` | 3 | 4 | 121 | 127 |
| data race | `SonobusAudioProcessor::doReceiveData (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::getRemotePeerPacketsReceived (Source/SonobusPluginProcessor.cpp)` | 3 | 2 | 85 | 69 |
| data race | `SonobusAudioProcessor::handleSinkEvents (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::updateSafetyMuting (Source/SonobusPluginProcessor.cpp)` |  |  | 80 | 74 |
| data race | `SonobusAudioProcessor::doReceiveData (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::handleSinkEvents (Source/SonobusPluginProcessor.cpp)` | 1 |  | 55 | 52 |
| data race | `SonobusAudioProcessor::doAddRemotePeerIfNecessary (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::getNumberRemotePeers (Source/SonobusPluginProcessor.cpp)` | 1 | 1 | 25 | 16 |
| data race | `push_aggregate (Source/RunningCumulant.c)` | `push_aggregate (Source/RunningCumulant.c)` |  |  | 10 | 22 |
| data race | `SonobusAudioProcessor::getRemotePeerConnected (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::handleSinkEvents (Source/SonobusPluginProcessor.cpp)` |  |  | 13 | 14 |
| data race | `SonobusAudioProcessor::setRemotePeerAutoresizeBufferMode (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::updateSafetyMuting (Source/SonobusPluginProcessor.cpp)` | 3 |  | 7 | 15 |
| data race | `SonobusAudioProcessor::updateSafetyMuting (Source/SonobusPluginProcessor.cpp)` | `push_aggregate (Source/RunningCumulant.c)` |  |  | 6 | 15 |
| data race | `SonobusAudioProcessor::getRemotePeerLatencyInfo (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::handleRemotePeerInfoUpdate (Source/SonobusPluginProcessor.cpp)` |  | 3 | 8 | 8 |
| data race | `SonobusAudioProcessor::getNumberRemotePeers (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::removeAllRemotePeersWithEndpoint (Source/SonobusPluginProcessor.cpp)` |  |  | 8 | 10 |
| data race | `SonobusAudioProcessor::handleSinkEvents (Source/SonobusPluginProcessor.cpp)` | `meon::core::EngineLink::peers (Source/meon/core/jam/EngineLink.cpp)` |  |  | 7 | 4 |
| data race | `push_aggregate (Source/RunningCumulant.c)` | `stats::RunCumulantor1D::resetInitVal (Source/RunCumulantor.h)` | 1 |  | 4 | 5 |
| data race | `SonobusAudioProcessor::handleSourceEvents (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::setRemotePeerSendActive (Source/SonobusPluginProcessor.cpp)` |  |  | 2 | 8 |
| data race | `SonobusAudioProcessor::AudioCodecFormatInfo::operator= (Source/SonobusPluginProcessor.h)` | `SonobusAudioProcessor::getRemotePeerLatencyInfo (Source/SonobusPluginProcessor.cpp)` |  |  | 7 | 3 |
| data race | `SonobusAudioProcessor::updateSafetyMuting (Source/SonobusPluginProcessor.cpp)` | `stats::RunCumulantor1D::resetInitVal (Source/RunCumulantor.h)` | 1 |  | 3 | 5 |
| data race | `SonobusAudioProcessor::doReceiveData (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::setRemotePeerRecvActive (Source/SonobusPluginProcessor.cpp)` | 1 |  | 3 | 2 |
| data race | `SonobusAudioProcessor::removeAllRemotePeersWithEndpoint (Source/SonobusPluginProcessor.cpp)` | `meon::core::EngineLink::meter (Source/meon/core/jam/EngineLink.cpp)` |  |  | 3 | 2 |
| data race | `SonobusAudioProcessor::connectRemotePeerRaw (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::setRemotePeerSendActive (Source/SonobusPluginProcessor.cpp)` |  |  | 3 | 2 |
| data race | `SonobusAudioProcessor::getRemotePeerLatencyInfo (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::handleSinkEvents (Source/SonobusPluginProcessor.cpp)` |  |  | 2 | 1 |
| data race | `SonobusAudioProcessor::handleOtherMessage (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::handleSinkEvents (Source/SonobusPluginProcessor.cpp)` |  |  |  | 3 |
| data race | `SonobusAudioProcessor::handleSinkEvents (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::setRemotePeerAutoresizeBufferMode (Source/SonobusPluginProcessor.cpp)` |  |  |  | 2 |
| data race | `SonobusAudioProcessor::doReceiveData (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::getRemotePeerRecvActive (Source/SonobusPluginProcessor.cpp)` |  |  | 1 |  |
| data race | `SonobusAudioProcessor::getRemotePeerRecvActive (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::handleSinkEvents (Source/SonobusPluginProcessor.cpp)` |  |  | 1 |  |
| data race | `SonobusAudioProcessor::connectRemotePeerRaw (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::getRemotePeerConnected (Source/SonobusPluginProcessor.cpp)` |  |  |  | 1 |
| data race | `SonobusAudioProcessor::getRemotePeerPacketsDropped (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::handleSinkEvents (Source/SonobusPluginProcessor.cpp)` |  |  |  | 1 |
| data race | `SonobusAudioProcessor::handleSinkEvents (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::setRemotePeerRecvActive (Source/SonobusPluginProcessor.cpp)` |  |  |  | 1 |
| data race | `SonobusAudioProcessor::getRemotePeerConnected (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::handleSourceEvents (Source/SonobusPluginProcessor.cpp)` |  |  |  | 1 |
| data race | `SonobusAudioProcessor::getRemotePeerSendActive (Source/SonobusPluginProcessor.cpp)` | `SonobusAudioProcessor::handleSourceEvents (Source/SonobusPluginProcessor.cpp)` |  |  |  | 1 |

### E. AOO 내부

| 종류 | 한쪽 | 다른 쪽 | main 2인 | E10 2인 | main 들고남 | E10 들고남 |
|---|---|---|---|---|---|---|
| data race | `aoo::lockfree::queue<float>::capacity (deps/aoo/lib/src/lockfree.hpp)` | `aoo::lockfree::queue<float>::resize (deps/aoo/lib/src/lockfree.hpp)` | 2 | 3 | 162 | 375 |
| data race | `aoo::net::peer::handle_message (deps/aoo/lib/src/client.cpp)` | `aoo::net::peer::send (deps/aoo/lib/src/client.cpp)` | 2 | 2 | 191 | 177 |
| data race | `aoo::lockfree::queue<float>::read_available (deps/aoo/lib/src/lockfree.hpp)` | `aoo::lockfree::queue<float>::resize (deps/aoo/lib/src/lockfree.hpp)` | 1 | 2 | 75 | 169 |
| data race | `aoo::net::ip_address::operator= (deps/aoo/lib/src/net_utils.hpp)` | `aoo::net::ip_address::operator== (deps/aoo/lib/src/net_utils.hpp)` | 2 | 2 | 120 | 120 |
| data race | `aoo::source_desc::add_packet (deps/aoo/lib/src/sink.cpp)` | `aoo::source_desc::process (deps/aoo/lib/src/sink.cpp)` | 1 | 2 | 118 | 120 |
| data race | `aoo::sink::handle_message (deps/aoo/lib/src/sink.cpp)` | `aoo::sink::setup (deps/aoo/lib/src/sink.cpp)` | 2 | 2 | 60 | 81 |
| data race | `aoo::source_desc::check_packet (deps/aoo/lib/src/sink.cpp)` | `aoo::source_desc::process (deps/aoo/lib/src/sink.cpp)` |  |  | 2 | 2 |
| data race | `aoo::source_desc::handle_format (deps/aoo/lib/src/sink.cpp)` | `aoo::source_desc::send_notifications (deps/aoo/lib/src/sink.cpp)` |  |  | 3 | 1 |
| data race | `aoo::source::process (deps/aoo/lib/src/source.cpp)` | `aoo::source::setup (deps/aoo/lib/src/source.cpp)` |  |  | 1 |  |
| data race | `aoo::lockfree::queue<float>::read_available (deps/aoo/lib/src/lockfree.hpp)` | `` |  |  |  | 1 |
| data race | `aoo::source_desc::handle_format (deps/aoo/lib/src/sink.cpp)` | `aoo::source_desc::set_protocol_flags (deps/aoo/lib/src/sink.hpp)` |  |  |  | 1 |
| data race | `aoo::source_desc::send_notifications (deps/aoo/lib/src/sink.cpp)` | `aoo::source_desc::set_protocol_flags (deps/aoo/lib/src/sink.hpp)` |  |  |  | 1 |

### F. JUCE JACK 장치 열기 (시작할 때)

| 종류 | 한쪽 | 다른 쪽 | main 2인 | E10 2인 | main 들고남 | E10 들고남 |
|---|---|---|---|---|---|---|
| data race | `juce::BigInteger::operator[] (modules/juce_core/maths/juce_BigInteger.cpp)` | `juce::StandalonePluginHolder::setupAudioDevices (Source/SonoStandaloneFilterWindow.h)` | 8 | 4 | 445 | 455 |
| data race | `juce::HeapBlock<unsigned int, false>::operator!= (modules/juce_core/memory/juce_HeapBlock.h)` | `juce::StandalonePluginHolder::setupAudioDevices (Source/SonoStandaloneFilterWindow.h)` | 4 | 4 | 238 | 240 |
| data race | `juce::BigInteger::operator= (modules/juce_core/maths/juce_BigInteger.cpp)` | `juce::BigInteger::operator[] (modules/juce_core/maths/juce_BigInteger.cpp)` | 3 | 3 | 3 | 1 |
| data race | `juce::BigInteger::operator[] (modules/juce_core/maths/juce_BigInteger.cpp)` | `memcpy (string_fortified.h)` | 2 | 4 | 3 | 1 |
| data race | `juce::HeapBlock<unsigned int, false>::free (modules/juce_core/memory/juce_HeapBlock.h)` | `juce::HeapBlock<unsigned int, false>::operator!= (modules/juce_core/memory/juce_HeapBlock.h)` | 1 | 2 | 1 | 1 |

## 크래시 (B 묶음, E10 전부터)

들어오고 나가기 TSan 시험에서 앱이 같은 자리에서 죽었다.

- main: A
- E10: A, 그리고 들어오고 나가던 C 가 2번

일반 빌드 들어오고 나가기 133번(E10)에서는 죽지 않았다. TSan 이 느려서 아래 틈이 넓어진 탓이다.

```
SEGV (읽기) foleys::LevelMeterSource::getNumChannels
  ← meon::core::EngineLink::peerLevel (EngineLink.cpp)
  ← JamSession::memberLevel ← JamScreen::MemberRow (미터 그리기, 화면 스레드)
```

- `SonobusAudioProcessor::getRemotePeerRecvMeterSource(i)`
  - 엔진 잠금(읽기)을 잡고 사람 i 의 미터 원본 **주소**만 돌려준다.
  - 화면은 잠금이 풀린 뒤 그 주소로 미터를 읽는다.
  - 그 사이 이벤트·받기 스레드가 그 사람을 지우면, 지워진 메모리를 읽는다.
- 같은 함수는 `index >= mRemotePeers.size()` 도 잠금 밖에서 본다. 그 사이 배열이 줄면 범위 밖을 읽는다 (위 heap-use-after-free 2건).
- E10 전에도 같은 코드였다 (PR ① 전에는 `indexOf` 로 번호를 찾고 같은 방식으로 읽었다).

**고칠 방안 (엔진 수정이라 승인 필요)**

- 엔진에 "잠금을 쥔 채 그 사람의 미터 값을 읽어 돌려주는" 함수를 하나 둔다. 예: `getRemotePeerRecvLevel(index, channel)`. 범위 확인도 잠금 안에서 한다.
- `EngineLink::peerLevel` 은 주소 대신 그 값을 쓴다.
- E10 뒤로 오디오 스레드는 이 잠금을 잡지 않는다. 그래서 화면이 읽기 잠금을 조금 더 오래 쥐어도 소리에는 영향이 없다.

→ E11 에서 고쳤다 (`docs/REMAKE_PLAN.md` 8.2 E11). 같은 시험에서 크래시 0번, 이 heap-use-after-free 0건.
