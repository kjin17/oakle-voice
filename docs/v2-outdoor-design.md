# v2 바깥 버전 「범용 짧은 질의」 설계

작성일 2026-10-07. 등급은 세 가지로 적는다. **[근거]**는 출처에 있는 내용, **[추론]**은 출처에서 끌어낸 내용, **미확인**은 확인하지 못한 내용이다. 실기 시험은 0회이고, VM에는 접속하지 않았다.

## 목표

휴대폰 핫스팟 Wi-Fi만 있으면 버튼을 누르고 말해서 짧은 답을 듣는다.
- 기기에는 Claude API 키를 넣지 않는다.
- 기기에는 **중계 서버 전용 기기 토큰**만 둔다.
- 하루 한도와 토큰 폐기는 서버가 맡는다.
- OpenClaw는 거치지 않는다. 범용 질의이고, 오클의 권한을 바깥에 열지 않기 위해서다.

## 구성도

```
[StickS3]  A버튼 누름 → 16kHz PCM 녹음 → 뗌
   │ HTTPS POST https://<voice 호스트>/v1/ask  (Bearer 기기토큰, X-Device-Id, audio/wav)
   │ 휴대폰 핫스팟 → 인터넷
   ▼
[오라클 VPS  nginx :443 (TLS 종단, 본문 상한 1MB, 요청 속도 제한)]
   │ proxy_pass http://127.0.0.1:8765
   ▼
[voice_bridge.py PROFILE=outdoor  (127.0.0.1 바인드)]
   ├─ 인증: 기기 토큰 sha256 대조, revoked 확인
   ├─ 한도: 기기별 하루 N회 (성공한 질의만 셈)
   ├─ STT: Groq whisper-large-v3-turbo  (OpenAI 호환 /audio/transcriptions)
   ├─ LLM: Claude Haiku 4.5, max_tokens 200, 「한두 문장」 시스템 프롬프트
   ├─ TTS: Google Cloud TTS ko-KR (LINEAR16 16kHz)
   └─ 기록: 시각·기기·상태·실패단계·단계별 ms·글자수·추정비용 (원문은 기본 미기록)
   │ 200 audio/wav
   ▼
[StickS3]  재생
```

`server/voice_bridge.py` 한 파일로 v1·v2를 모두 돌린다. `PROFILE`만 바꾸면 된다. 기기 펌웨어도 같은 것을 쓰고, `config.h`의 URL과 TLS 설정만 다르다.

## 엔드포인트와 규약

| 메서드 | 경로 | 설명 |
|---|---|---|
| POST | `/v1/ask` | 본문은 16kHz/16bit/mono WAV이고 15초가 상한이다. 응답은 `200 audio/wav`, 헤더 `X-Transcript`·`X-Answer`는 URL 인코딩되어 있다 |
| GET | `/healthz` | `ok` |

| 코드 | 의미 |
|---|---|
| 401 | 토큰 불일치 또는 폐기됨 |
| 413 | 길이 초과 |
| 429 | 하루 한도 초과 |
| 204 | 받아 적은 글이 비어 있음 |
| 502 | 하위 단계 실패. 본문에 실패 단계가 `stt`, `llm`, `tts` 중 하나로 들어간다. 기기 LCD에 그대로 표시한다 |

관리 명령은 서버에서 직접 실행한다.
- `python3 voice_bridge.py add-device sticks3-2`: 토큰을 발급한다. 토큰은 이때 한 번만 출력된다.
- `python3 voice_bridge.py revoke-device sticks3-2`: 즉시 폐기한다.

## 비용

가격은 전부 2026-10-07에 공식 가격 페이지에서 확인했다.

| 항목 | 단가 | 출처 | 등급 |
|---|---|---|---|
| Claude Haiku 4.5 | 입력 $1/MTok, 출력 $5/MTok | https://platform.claude.com/docs/en/about-claude/pricing | [근거] |
| Groq whisper-large-v3-turbo | $0.04/시간, **요청당 최소 10초 과금** | https://console.groq.com/docs/speech-to-text | [근거] |
| OpenAI gpt-4o-mini-transcribe | $0.003/분 | https://developers.openai.com/api/docs/pricing | [근거] |
| OpenAI whisper-1 | $0.006/분 | 같은 곳 | [근거] |
| Deepgram nova-3 (ko 지원) | $0.0048/분 (스트리밍 한시 프로모션가, 정가 $0.0077) | https://deepgram.com/pricing , https://developers.deepgram.com/docs/models-languages-overview | [근거] |
| Google STT V2 Standard | $0.016/분, 1초 단위 올림 | https://cloud.google.com/speech-to-text/pricing | [근거] |
| Google TTS WaveNet/Standard | $4/100만 자, **월 400만 자 무료** | https://cloud.google.com/text-to-speech/pricing | [근거] |
| Google TTS Neural2 | $16/100만 자, **월 100만 자 무료** | 같은 곳 | [근거] |
| Google TTS Chirp 3 HD | $30/100만 자, 월 100만 자 무료 | 같은 곳 | [근거] |
| ko-KR 음성 수 | Chirp3 30, Neural2 3, WaveNet 4, Standard 4 | https://cloud.google.com/text-to-speech/docs/list-voices-and-types | [근거] |
| OpenAI tts-1 | $15/100만 자 | OpenAI 가격 페이지 | [근거] |
| Azure Neural TTS | $15/100만 자, F0 무료 월 50만 자 | https://prices.azure.com/api/retail/prices (eastus) | [근거] |

### 질의 1회 예상 비용

조건: 음성 5초, Haiku 입력 300토큰과 출력 100토큰, 답 60자. 계산은 전부 [추론]이다.

| 조합 | STT | LLM | TTS | 1회 합계 |
|---|---|---|---|---|
| **추천: Groq turbo + Haiku + Google Neural2** | $0.00011 | $0.0008 | $0.00096 (무료 한도 안이면 $0) | **약 $0.0009** (한도 밖이면 $0.0019) |
| Groq + Haiku + WaveNet | $0.00011 | $0.0008 | $0.00024 | 약 $0.0012 |
| OpenAI 단일 (mini-transcribe + Haiku + tts-1) | $0.00025 | $0.0008 | $0.0009 | 약 $0.002 |
| Google 단일 (STT V2 + Haiku + Neural2) | $0.0013 | $0.0008 | $0.00096 | 약 $0.003 |

- 하루 50회 한도에 매일 꽉 채우면 월 약 1,500회다. 추천 조합으로 약 **$1.4/월**이다 [추론].
- 무료 한도는 Neural2 월 100만 자다. 60자짜리 답이면 약 1.6만 회분이라, 이 용도에서는 TTS가 사실상 무료다 [추론].
- 한국어를 문자 1개로 세는지 바이트로 세는지는 미확인이다. 문서에는 "characters"라고만 적혀 있다.

## 계정·키 필요 사항

이번 작업에서는 아무것도 만들지 않았다. 아래는 결정할 항목이다.

| 필요 | 비고 |
|---|---|
| Anthropic API 키 (중계 서버 전용) | 구독용 토큰을 재사용하지 말고 **VM 전용 API 키를 따로 발급하고 Console에서 월 지출 상한을 거는** 쪽을 권한다. 미발급 |
| STT 키 | Groq 계정이 새로 필요하다. 계정을 늘리지 않으려면 OpenAI 키를 쓰거나, 이미 있는 `gemini-api`로 Gemini 오디오 이해를 쓸 수 있다. Gemini 단가는 **미확인**이다 |
| TTS 키 | Google Cloud 프로젝트와 API 키가 필요하다. 계정을 늘리지 않으려면 기존 ElevenLabs 키(유료)나 Microsoft Edge TTS(무료지만 SLA 없음)를 쓸 수 있다. Edge TTS는 [근거: OpenClaw docs/tools/tts/quickstart.md "Best-effort, no SLA"] |

키는 VM의 `~/.openclaw/credentials/` 같은 파일이나 systemd `EnvironmentFile`에 둔다. 코드와 리포에는 넣지 않는다. `read_secret()`은 환경변수를 먼저 보고, 없으면 파일을 읽는다.

## 오라클 VM 배치 제안

접속하지 않았고, 아래는 제안만이다.

| 안 | 내용 | 장점 | 단점 |
|---|---|---|---|
| **① 기존 VPS nginx에 서브도메인 추가** (예: `voice.<도메인>`) | Let's Encrypt 인증서를 받고 `proxy_pass 127.0.0.1:8765`로 넘긴다. 서버는 systemd 유닛으로 띄운다 | 새 포트를 열 필요가 없다. TLS·속도 제한·본문 상한을 nginx 한곳에서 관리한다 | 오클 사무실과 같은 nginx를 쓰므로 설정 실수가 사무실에 번질 수 있다 [추론] |
| ② VPS 별도 포트 (예: 8443 + 자체 TLS) | nginx 바깥에 둔다 | 사무실 설정과 완전히 분리된다 | OCI 보안 목록에 포트를 새로 열어야 한다. 인증서를 따로 관리해야 한다 |

**추천은 ①**이다 [추론]. 단, 서브도메인을 Cloudflare 프록시 뒤에 두면 막힐 수 있다. 과거 Cloudflare Bot Fight류 규칙이 특정 UA(Python-urllib)를 막은 기록이 있다. ESP32 HTTPClient의 UA(`ESP32HTTPClient`)도 막히는지는 **미확인**이다. 처음에는 DNS only(회색 구름)로 시작하기를 권한다 [추론].

nginx 쪽 초안은 `server/nginx-voice.conf.example`에 있다.

## 배터리 (250mAh)

- 수치 출처를 찾지 못했다. 미확인이다. Wi-Fi를 연결한 채 대기하면 수십 mA, 송수신 중에는 100mA를 넘는 수준으로 본다. 그러면 계속 켜 둘 때 2~4시간 정도다 [추론].
- 대책은 다음과 같다. 모두 [추론]이다.
  1. 대기 중에는 light sleep이나 Wi-Fi 끄기를 쓰고, A버튼(GPIO11, RTC 가능 핀)으로 깨운다. 깨어나 재연결하는 데 1~3초가 걸린다.
  2. 화면은 녹음이나 재생 중에만 켜고 백라이트를 낮춘다.
  3. 일정 시간(예: 2분) 동안 쓰지 않으면 PMIC 전원을 끈다.
- 도착 후 실측한다. 대기, 질의 1회, 완충 후 횟수를 잰다.

## 보안·남용 방지

- 토큰은 기기마다 다르다. 서버에는 해시만 저장한다. 분실하면 `revoke-device`로 폐기한다.
- 하루 한도의 기본값은 50회다(`DAILY_LIMIT`). 실패한 요청은 한도를 깎지 않는다. 대신 nginx `limit_req`로 무차별 시도를 막는다.
- Anthropic Console의 월 지출 상한이 마지막 안전장치다.
- 로그에 남기는 것은 상태, 단계, 소요 ms, 글자 수, 추정 비용이다. 원문(질문과 답)은 기본적으로 남기지 않는다. 디버깅할 때만 `LOG_TEXT=1`로 켠다.
- 오클의 도구 권한과는 완전히 분리되어 있다. 이 서버는 OpenClaw를 부르지 않는다.

## 펌웨어 대안: xiaozhi 프로토콜 재사용

xiaozhi-esp32는 StickS3를 공식 보드로 지원한다 [근거: github.com/78/xiaozhi-esp32 `main/boards/m5stack/stick-s3`]. 프로토콜 구성은 다음과 같다 [근거: docs/websocket.md].
- WebSocket 연결과 Bearer 헤더
- hello 교환
- Opus 16kHz, 60ms 프레임
- JSON `listen`(manual 모드)과 `stt`/`llm`/`tts` 이벤트

xinnan-tech/xiaozhi-esp32-server(Python, MIT)는 OpenAI 호환 LLM을 붙일 수 있다 [근거]. Haiku는 OpenAI 호환 엔드포인트로 붙이거나 어댑터를 거쳐야 한다. Anthropic의 OpenAI 호환 지원 범위는 미확인이다.

| 기준 | xiaozhi 재사용 | 자체 HTTP 펌웨어 |
|---|---|---|
| 장점 | 스트리밍이라 첫 소리가 빠르다. 펌웨어를 새로 쓸 필요가 없다. 기존 UI·호출어가 있다 | 우리가 다 읽을 수 있는 분량이다. v1과 같은 펌웨어다. 서버도 표준 라이브러리만 쓴다 |
| 단점 | 서버가 무겁다(Docker, 여러 구성요소). 기기 활성화 흐름과 우리 토큰·한도 체계를 맞춰야 한다 | 녹음이 끝난 뒤에야 처리를 시작하므로 체감 지연이 늘어난다 |

결론: **1차는 자체 HTTP, 2차로 지연이 불만이면 xiaozhi로** [추론].

## 미확인 (v2)

- 핫스팟 환경에서의 실제 왕복 지연
- ESP32 TLS 핸드셰이크 시간. 요청마다 새로 연결하면 1~2초가 걸린다고 본다 [추론]
- Groq와 Google의 한국어 품질 비교
- Gemini를 STT·TTS 대안으로 쓸 때의 단가
- Cloudflare 경유 시 ESP32 UA가 차단되는지
- 250mAh로 실제 몇 회를 쓸 수 있는지
