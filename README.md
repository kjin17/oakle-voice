# oakle-voice — M5Stack StickS3 음성 리모컨 2종

작성일: 2026-10-07. 기기 2대는 도착 전이고, 이 저장소는 조사와 초안만 담고 있다. 실기·컴파일 시험은 하지 않았다. 원격 저장소는 없다.

등급 표기
- **[근거]**: 출처에 적혀 있음
- **[추론]**: 출처에서 미루어 본 것
- **미확인**: 아직 확인하지 못함

출처는 각 문서에 적어 두었다.

| 버전 | 한 줄 요약 | 문서 |
|---|---|---|
| **v1 집 안 「오클 음성 리모컨」** | 버튼을 누르고 말하면 맥미니가 받아 적고(whisper.cpp), **OpenClaw main(오클)** 에 넘긴 뒤 답을 `say -v Yuna`로 읽어 기기 스피커로 돌려준다 | [docs/v1-home-design.md](docs/v1-home-design.md) |
| **v2 바깥 「범용 짧은 질의」** | 휴대폰 핫스팟에서 HTTPS로 오라클 VPS 중계 서버에 보내면 Groq STT → Claude Haiku 4.5 → Google TTS를 거쳐 답한다. 기기에는 중계 서버 토큰만 있다 | [docs/v2-outdoor-design.md](docs/v2-outdoor-design.md) |
| 하드웨어 | 핀맵. 앰프와 LCD·오디오 전원은 **PMIC(M5PM1) 경유**다 | [docs/sticks3-hardware.md](docs/sticks3-hardware.md) |
| Muse SDK 대조 | Meta Muse Gadget SDK 는 참고·벤치마크만 한다(코드 이식 안 함). 하드웨어 정정, 배운 설계, 펌웨어 방향, 벤치마크 계획 | [docs/muse-sdk-benchmark.md](docs/muse-sdk-benchmark.md) |

## 구성도

```
v1 (LAN 전용, Pi/HA 경로 밖)
 StickS3 ──HTTP POST wav──▶ 맥미니:8765 voice_bridge(home)
                              ├ whisper-cli (Metal, ko)
                              ├ 127.0.0.1:18789 /v1/chat/completions → 오클(main)
                              └ say -v Yuna → afconvert 16k wav ──▶ StickS3 스피커

v2 (인터넷, 핫스팟)
 StickS3 ──HTTPS POST wav──▶ VPS nginx:443 ─▶ 127.0.0.1:8765 voice_bridge(outdoor)
                                               ├ 기기토큰·하루한도·폐기·로그(sqlite)
                                               ├ Groq whisper-large-v3-turbo
                                               ├ Claude Haiku 4.5 (max_tokens 200)
                                               └ Google TTS ko-KR ──▶ StickS3 스피커
```

## 추천안

1. **펌웨어는 하나로 둘 다 쓴다**: `firmware/oakle_voice/`, Arduino + M5Unified, 버튼을 누르는 동안 녹음해서 HTTP POST로 보낸다. `config.h`의 URL과 TLS 설정만 바꾸면 v1도 되고 v2도 된다. M5Unified가 StickS3의 PMIC와 앰프를 알아서 처리하므로 핀을 직접 만지지 않아도 된다 [근거: M5Unified `board_M5StickS3`] / 실제로 동작하는지는 [추론].
2. **서버도 하나로 쓴다**: `server/voice_bridge.py`, 표준 라이브러리만 사용, `PROFILE=home|outdoor`로 전환한다.
3. **HA Assist 경로(ESPHome)는 대안으로만 둔다**: `esphome/sticks3-v1.yaml`. 이 경로는 Pi가 오디오를 중계해야 하고, Extended OpenAI Conversation 통합(HACS)도 추가로 깔아야 한다.
4. **오클에 넣는 공식 경로는 Gateway의 OpenAI 호환 `/v1/chat/completions`다** (`model: openclaw/main`) [근거: `docs/gateway/openai-http-api.md`]. 지금은 **꺼져 있다**. Gateway 토큰은 운영자 전체 권한과 같으므로 기기에 넣지 않고, 맥미니 중계 서버만 갖고 있게 한다.

## 결정이 필요한 항목

1. **v1 전송 방식**: A(커스텀 펌웨어 → 맥미니, 추천) / B(ESPHome → HA Assist) / C'(xiaozhi 펌웨어 + 자체 xiaozhi 서버에 OpenClaw를 LLM으로 연결) 중 하나.
2. **OpenClaw 설정 변경 승인**: `gateway.http.endpoints.chatCompletions.enabled: true`. 승인하지 않으면 느린 `openclaw agent` CLI 경로로 시험한다.
3. **음성으로 들어온 질의의 권한**: 오클의 모든 도구 권한을 그대로 쓸지, 아니면 「음성 질의는 조회만, 바꾸는 일은 텔레그램에서 확인」 같은 규칙을 오클 지시문에 둘지. 집 안에서는 누가 버튼을 눌러도 같은 권한으로 실행된다.
4. **v1 세션**: 기기 전용 세션(추천, 빠름) / 텔레그램 main 세션과 공유.
5. **v1 응답 모델**: 오클의 기본 모델(opus) 그대로 / 음성 질의만 빠른 모델(`x-openclaw-model`).
6. **v2 키와 계정**: ① Anthropic **VM 전용 API 키 + 월 지출 상한** 발급, ② STT는 Groq 신규 가입(최저가) / OpenAI / 기존 Gemini 키, ③ TTS는 Google Cloud 신규(무료 한도 안) / 기존 ElevenLabs / Edge TTS(무료지만 SLA 없음).
7. **v2 배치**: 기존 VPS nginx에 서브도메인 추가(추천) / 별도 포트. 도메인 이름과 Cloudflare 프록시 사용 여부(처음엔 DNS only 권장).
8. **v2 하루 한도**: 기기당 기본 50회. 추천 조합으로 약 $0.001/회, 꽉 채워도 월 $1.4 정도다 [추론].
9. **호출어**: 쓰지 않음(추천, 배터리) / 영어 기성 모델 / 「오클」 직접 학습. ESP-SR 한국어 웨이크워드는 「planned」 상태다 [근거].
10. **기기 2대 배분**: 집 1대 + 바깥 1대 / 둘 다 같은 펌웨어로 두고 그때그때 전환.

## 도착 후 첫 단계

1. USB-C로 연결해서 전원이 들어오는지, 시리얼 포트가 보이는지 확인한다. 출하 UiFlow2 는 USB 시리얼을 꺼 둔다는 보고가 있다. **굽기 전에 8MB 전체를 백업한다**(`docs/sticks3-hardware.md` 「다운로드 모드」 줄).
2. M5Unified의 `examples/Basic/Microphone` 예제와 Speaker 예제를 올려 **녹음과 재생이 되는지** 확인한다. 핀이 맞는지와 PMIC를 거치는 앰프가 실제로 켜지는지는 이걸로 판정한다.
3. 맥미니에서 `voice_bridge.py`를 `PROFILE=home`, `LLM=cli`로 손으로 띄우고, curl로 WAV를 보내 왕복이 되는지 시험한다. 설정 변경 없이 할 수 있다(`server/README.md`).
4. `firmware/oakle_voice`를 올려 v1 왕복을 끝까지 해 보고, 단계별 ms를 시리얼 `timing ms:` 줄과 sqlite `requests` 표에서 읽는다. 앰프 시험은 전원 버튼 두 번(완전 끄기) 뒤에 한다.
5. `docs/muse-sdk-benchmark.md` §4 순서로 벤치마크한다.
6. 배터리를 잰다. 완충 상태에서 대기 시간과 질의 1회당 소모를 본다. → v2 sleep 설계를 확정한다.

## 미확인 요약

- 실기 동작 전부: 녹음 게인, 스피커 음량, M5Unified StickS3 분기가 실제로 동작하는지
- 오클 턴의 실제 지연, `openclaw agent --json` 출력 형식, `user`에서 파생되는 세션 키 이름
- 한국어 STT 품질 비교 (whisper.cpp vs mlx, Groq vs OpenAI)
- 250mAh 배터리로 버티는 시간
- ESP32 UA가 Cloudflare에서 차단되는지
- Gemini를 STT/TTS로 쓸 때의 단가
- GPIO13의 용도 (회로도 PDF를 보지 않음). LED 는 PMIC 0x06 bit4 로 확인됨

## 파일

```
README.md                         이 문서
docs/sticks3-hardware.md          핀맵·출처
docs/v1-home-design.md            v1 설계
docs/v2-outdoor-design.md         v2 설계·비용표
docs/muse-sdk-benchmark.md        Muse SDK 대조·벤치마크 계획
firmware/oakle_voice/             Arduino + M5Unified 펌웨어 초안 (config.h.example)
server/voice_bridge.py            중계 서버 초안 (home/outdoor)
server/nginx-voice.conf.example   v2 nginx 초안
esphome/sticks3-v1.yaml           경로 B(HA Assist) 대안 초안
esphome/secrets.yaml.example      값 없는 예시
```
