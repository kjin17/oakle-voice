# v1 집 안 버전 「오클 음성 리모컨」 설계

작성일: 2026-10-07. 등급 표기: **[근거]** 출처에 있음 · **[추론]** 출처에서 미루어 봄 · **미확인** 찾지 못했거나 아직 실측 안 함. 실기 시험은 0회다.

## 목표

StickS3의 버튼을 누르고 말하면 맥미니가 받아 적는다(STT). 받아 적은 글은 **OpenClaw main 에이전트(오클)** 에게 그대로 넘긴다. 일반 Claude 호출이 아니다. 오클의 답은 한국어 음성(TTS)으로 바꿔 StickS3 스피커로 돌려준다. HA가 도는 Pi 3B는 남은 메모리가 98MB라 오디오 처리 경로에 넣지 않는다.

## 1. OpenClaw에 텍스트를 넣고 답을 받는 공식 경로

| 경로 | 내용 | 출처 | 판정 |
|---|---|---|---|
| **Gateway `/v1/chat/completions`** (OpenAI 호환) | `model: "openclaw/main"`을 주면 main 에이전트로 간다. 요청은 일반 에이전트 실행과 같은 코드 경로(`openclaw agent`와 동일)를 탄다. `user` 문자열을 고정하면 Gateway가 그 값에서 세션 키를 파생해 대화가 이어진다. **기본값은 꺼짐**이고 `gateway.http.endpoints.chatCompletions.enabled: true`로 켠다. | `docs/gateway/openai-http-api.md` | **채택** [근거] |
| `openclaw agent` CLI | `--agent main --session-key … --message … --json`. 설정을 바꾸지 않아도 된다. | `openclaw agent --help` (2026.9.7) | 예비안 [근거]. 요청마다 node가 새로 뜨므로 느리다 [추론]. `--json` 출력 스키마는 미확인 |
| `/v1/responses` (OpenResponses) | 따로 켜야 한다(`gateway.http.endpoints.responses.enabled`). | `docs/gateway/openresponses-http-api.md` | chat completions로 충분하다 [추론] |
| Talk mode / 음성 노드 | macOS·iOS·Android·Watch 네이티브 앱용이다. 노드는 Gateway WS 프로토콜과 페어링을 거쳐야 한다. | `docs/nodes/talk.md`, `docs/nodes/index.md` | ESP32가 그 프로토콜을 구현하는 건 과하다 [추론] |
| 내장 오디오 전사 | 채널로 들어온 음성 첨부를 자동 전사한다(`whisper-cli`, sherpa-onnx, `whisper` 순으로 자동 선택). | `docs/nodes/audio.md` | 채널 첨부용이다. 이 장치에는 중계 서버가 직접 STT를 돌리는 쪽이 단순하다 [추론] |
| TTS | `tts.convert` RPC가 있고, 공급자로 Microsoft Edge·ElevenLabs·Local CLI 등을 쓸 수 있다. | `docs/tools/tts/api.md`, `docs/tools/tts/quickstart.md` | 쓰려면 WS RPC를 붙여야 한다. macOS `say`가 더 단순하다 [추론] |

현재 설정(`~/.openclaw/openclaw.json`, 읽기만 함, 2026-10-07):
- `gateway.bind = loopback`, `port = 18789`, `auth.mode`는 토큰(SecretRef 파일)이다.
- `gateway.http` 칸이 없다. 즉 chatCompletions 엔드포인트는 꺼져 있다.
- `talk.agentId = main`이다.
→ StickS3는 loopback에 닿을 수 없다. 그래서 **맥미니 위의 중계 프로세스가 LAN에서 요청을 받아 loopback Gateway로 넘긴다** [추론]. Gateway 토큰은 맥미니 밖으로 나가지 않는다.

> ⚠️ 보안 경계 [근거: openai-http-api.md "Security boundary"]
> 이 엔드포인트의 Gateway 토큰은 **운영자(owner) 전체 권한**과 같다. 그래서 기기에는 Gateway 토큰을 절대 넣지 않는다. 기기는 **중계 서버 전용 토큰**만 가진다. 중계 서버는 받아 적은 텍스트만 넘기고, 헤더(`x-openclaw-model`·`x-openclaw-session-key` 등)는 기기에서 받지 않는다.
> 그래도 남는 위험이 있다. 음성으로 들어온 문장이 오클의 모든 도구 권한으로 실행된다. 집 안에서 남이 버튼을 눌러도 똑같다 [추론]. → 결정 항목 3 참고.

## 2. 기기 ↔ 맥미니 전송 방식 비교

| 기준 | A. 커스텀 펌웨어 → 맥미니 HTTP 중계 | B. ESPHome voice_assistant → HA Assist → (Wyoming STT/TTS @맥미니) + 커스텀 conversation agent | C. 맥미니에 Wyoming 서버 + 기기가 직접 Wyoming |
|---|---|---|---|
| Pi 부하 | **0** | 기기 오디오가 HA를 거쳐 오간다. 통합도 추가로 깔아야 한다(Extended OpenAI Conversation은 HACS 필요) | 0 |
| 근거 | — | voice_assistant는 "stream the audio to Home Assistant", HA가 필수다 [근거: esphome.io/components/voice_assistant]. HA 내장 OpenAI 통합은 호환 서버를 지원하지 않는다 [근거: home-assistant.io/integrations/openai_conversation]. Extended OpenAI Conversation에는 Base Url이 있다 [근거: github.com/jekalmin/extended_openai_conversation] | ESP32용 Wyoming 클라이언트 컴포넌트를 찾지 못했다(미확인). wyoming-satellite는 DEPRECATED다 [근거: github.com/rhasspy/wyoming-satellite] |
| 지연 | 녹음을 끝까지 한 뒤 한 번에 업로드한다. LAN이면 업로드가 1초 미만이다 [추론] | 스트리밍이라 STT는 빠를 수 있다. 대신 HA를 한 번 더 거친다 [추론] | — |
| 구현량 | 펌웨어 약 150줄 + 중계 서버 약 300줄(초안 있음) | YAML(초안 있음) + HA 설정 + 통합 설치 + Wyoming 서버 2개 | 기기 쪽 클라이언트를 처음부터 짜야 한다 |
| 보안(LAN만) | 중계 서버를 맥미니 LAN IP에만 바인드하고 기기 토큰을 쓴다 | HA API 암호화 키, HA→맥미니 구간은 따로 인증해야 한다 | — |
| v2와 공유 | **펌웨어를 그대로 쓴다**(URL과 TLS만 바뀜) | 바깥에서는 못 쓴다(HA 필수) | — |

**추천: A** [추론]
- Pi를 경로에서 완전히 뺄 수 있다.
- v2와 펌웨어를 하나로 쓸 수 있다.
- 조사한 범위에서는 OpenClaw를 호출할 때 HA를 거치는 쪽이 오히려 부품이 더 많다.

B는 호출어와 HA 대시보드 연동이 꼭 필요해질 때를 위한 대안으로 둔다. 그래서 `esphome/sticks3-v1.yaml` 초안을 남겨 두었다.

참고로 xiaozhi-esp32는 StickS3를 공식 보드로 지원한다 [근거: github.com/78/xiaozhi-esp32 `boards/m5stack/stick-s3`]. 서버를 직접 띄우는 xinnan-tech/xiaozhi-esp32-server(Python, OpenAI 호환 LLM 지원 [근거])에 OpenClaw를 LLM으로 붙이는 길도 있다. 그러면 펌웨어는 한 줄도 안 짜도 된다. 대신 그 서버가 Docker와 다수 구성요소를 끌고 온다. 펌웨어를 우리 손으로 고치기 어려운 점, 토큰 대신 기기 활성화 흐름을 쓰는 점도 부담이다 [추론]. → 결정 항목 1의 「C'」.

## 3. 맥미니 로컬 STT / TTS (비용 0)

맥미니는 Apple M4, 메모리 16GB다(sysctl 확인). 2026-10-07 기준 `whisper-cli`, `whisper`, `mlx_whisper`, `ffmpeg`는 **설치되어 있지 않다**.

| 후보 | 장점 | 단점 | 등급 |
|---|---|---|---|
| **whisper.cpp (`whisper-cli`) + large-v3-turbo** | Metal 가속을 쓴다. OpenClaw의 오디오 자동 선택이 1순위로 찾는 이름(`whisper-cli`)이라 나중에 채널 음성 전사와 같은 바이너리를 같이 쓸 수 있다 | 모델 파일(약 1.6GB)을 받아야 한다 [추론] | Metal은 [근거: github.com/ggml-org/whisper.cpp], 자동 선택은 [근거: docs/nodes/audio.md] |
| mlx-whisper | 개인 벤치마크에서 whisper.cpp보다 약 2배 빨랐다 | Python venv가 따로 필요하다 | [추론] 블로그 출처: notes.billmill.org/dev_blog/2026/01/… |
| faster-whisper | — | Mac에서는 CPU로만 돈다 | [근거/추론] |
| sherpa-onnx 한국어 zipformer / SenseVoice | 가볍다 | 품질 비교 자료가 없다 | 모델 존재는 [근거], 품질은 미확인 |

- 한국어 품질과 속도의 공식 벤치마크는 **미확인**이다. 도착 후 같은 녹음 10개로 whisper.cpp와 mlx를 직접 비교한다.
- 5초 발화를 1초 안팎으로 받아 적을 것으로 보지만 [추론], 실측한 값은 아니다.

| TTS 후보 | 한국어 | 비고 |
|---|---|---|
| **macOS `say -v Yuna`** + `afconvert` | 있음. Yuna 외에 Eddy·Flo·Reed·Sandy·Shelley 등도 있다(`say -v '?'` 확인) | 설치가 필요 없다. 2026-10-07 시험: 「안녕하세요, 오클입니다.」가 16kHz mono WAV 1.85초로 나왔다 [근거: 직접 실행] |
| Piper | `ko_KR-kss-medium` 하나뿐 | [근거: huggingface.co/rhasspy/piper-voices] |
| MeloTTS | 지원, MIT 라이선스 | [근거: github.com/myshell-ai/MeloTTS]. Python과 torch가 무겁다 [추론] |
| Kokoro-82M | 한국어 없음 | [근거: VOICES.md] |
| ElevenLabs | 품질이 좋다. | 유료라 비용 0 원칙에서 벗어난다. 선택지로만 둔다 |

**추천: STT는 whisper.cpp large-v3-turbo, TTS는 `say -v Yuna`** [추론]. 둘 다 `server/voice_bridge.py`에서 설정값 하나로 바꿀 수 있다(`STT=mlx`, `SAY_VOICE=…`).

## 4. 구성도

```
[StickS3]  A버튼 누름 → 16kHz PCM 녹음(PSRAM, ≤10초) → 뗌
   │  HTTP POST /v1/ask  (audio/wav, Bearer 기기토큰, X-Device-Id)   ← 집 Wi-Fi, LAN 전용
   ▼
[맥미니 192.168.1.50:8765  voice_bridge.py PROFILE=home]
   ├─ STT: whisper-cli -l ko (Metal)
   ├─ LLM: POST http://127.0.0.1:18789/v1/chat/completions
   │        model=openclaw/main, user=voice:<device_id>   (Gateway 토큰은 credentials 파일에서)
   │        └→ OpenClaw main(오클) 에이전트 턴: 기억·도구·HA MCP 그대로
   ├─ 말하기용 정리: 마크다운·링크·코드 제거, 400자 상한
   └─ TTS: say -v Yuna → afconvert 16kHz mono WAV
   │  200 audio/wav (+ X-Transcript, X-Answer 헤더 → LCD)
   ▼
[StickS3]  스피커 재생 → READY
(Pi 3B / HA: 경로에 없음. 집 기기 제어가 필요하면 오클이 이미 가진 HA MCP로 한다)
```

## 5. 지연 예산 [추론, 실측 전]

| 단계 | 예상 |
|---|---|
| 녹음 끝 → 업로드 (5초 = 160KB, LAN) | 0.2~0.5초 |
| STT (whisper.cpp turbo, M4) | 0.5~1.5초 |
| **오클 에이전트 턴** | **5~30초.** main은 opus이고 컨텍스트가 크다. 지배 요인이다 |
| TTS (say + afconvert) | 0.3~1초 |
| 다운로드 + 재생 시작 | 0.3초 |

오클 턴이 대부분을 차지한다. 줄이는 방법은 세 가지다.
1. 기기 전용 세션(`user=voice:sticks3-1`)으로 컨텍스트를 작게 유지한다.
2. 질문 앞에 「두세 문장, 마크다운 없이」 지시를 붙인다(초안의 `VOICE_PREFIX`).
3. 필요하면 `x-openclaw-model`로 빠른 모델을 쓴다. 이건 결정 항목이다.

기기 HTTP 타임아웃은 120초로 잡았다.

## 6. 세션 설계 — 결정 필요

- (가) **기기 전용 세션**(초안 기본값, `user=voice:<device_id>`)
  - 텔레그램 대화와 섞이지 않는다.
  - 컨텍스트가 작아 빠르다 [추론].
  - 대신 「아까 텔레그램에서 말한 거」는 모른다. 기억 파일과 memory_search로는 볼 수 있다.
- (나) **텔레그램 main 세션에 합류**(`x-openclaw-session-key`로 그 세션 키 지정)
  - 맥락을 공유한다.
  - 대신 음성 질의가 텔레그램 기록에 섞이고, 그 큰 컨텍스트를 매번 태운다.
  - 세션 키 형식과 owner 의미론은 미확인이다.

## 7. 보안

- 중계 서버는 `BIND=192.168.1.50`(LAN IP)에만 연다. 공유기 포트포워딩은 하지 않는다.
- 기기 토큰은 sqlite에 sha256 해시만 저장한다. `revoke-device` 명령으로 즉시 폐기한다.
- 질의 원문은 기본적으로 로그에 남기지 않는다(`LOG_TEXT=0`). 단계별 소요시간과 상태만 남긴다.
- 기기 분실 대책: 토큰을 폐기하면 끝난다. Gateway 토큰은 기기에 없다.
- 남는 위험: 음성 문장이 오클의 도구 권한으로 실행된다. 결정 항목 3에서 정한다.

## 8. 설치할 것 (도착 후, 승인 뒤 — 이번 작업에서는 하지 않음)

1. `brew install whisper-cpp`(바이너리 이름 `whisper-cli`)를 설치하고 `ggml-large-v3-turbo.bin`을 받는다.
2. `~/.openclaw/openclaw.json`에 `gateway.http.endpoints.chatCompletions.enabled: true`를 넣는다. 이건 **설정 변경이라 승인이 필요하다.** 넣은 뒤 `curl 127.0.0.1:18789/v1/models`로 `openclaw/main`이 보이는지 확인한다.
3. `voice_bridge.py add-device sticks3-1`로 토큰을 발급하고, 기기 `config.h`에 넣는다.
4. 중계 서버를 상시 실행할지(LaunchAgent) 정한다. 먼저 손으로 띄워서 시험한다.

## 미확인 (v1)

- `openclaw agent --json` 출력 스키마
- `user`에서 파생한 세션 키의 실제 이름과 수명
- 오클 턴의 실제 지연
- `say` 음성 가운데 무엇이 StickS3 소형 스피커에서 가장 잘 들리는지
- M5Unified `Mic.record`의 실제 녹음 품질과 게인. 아날로그 마이크라 게인 조정이 필요할 수 있다 [추론]
