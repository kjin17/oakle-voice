# server/ — 중계 서버 초안

`voice_bridge.py` 하나로 두 프로필을 돈다. 표준 라이브러리만 쓰고, Python 3.9 이상이면 된다.

| 프로필 | 바인드 | STT | LLM | TTS |
|---|---|---|---|---|
| `PROFILE=home` (v1) | `BIND=192.168.1.50` (맥미니 LAN) | `whisper-cli` (또는 `STT=mlx`) | OpenClaw `/v1/chat/completions` loopback (또는 `LLM=cli`) | `say -v Yuna` → `afconvert` |
| `PROFILE=outdoor` (v2) | `127.0.0.1` (nginx 뒤) | Groq/OpenAI 호환 `/audio/transcriptions` | Claude Haiku 4.5 | Google Cloud TTS |

## 검증 상태 (2026-10-07)

**실행해 본 것.** 임시 STATE_DIR에서 import한 뒤 아래 함수를 직접 호출했다.
- `to_speakable`
- `tts_macos_say`: 16kHz mono WAV 1.85초 생성
- `add_device`, `check`, `consume`, `record`

**안 해 본 것.**
- STT 3종
- LLM 3종. Gateway 엔드포인트가 꺼져 있고 키도 없다
- HTTP 서버 기동
- 기기에서 오는 실제 요청

## 비밀값

비밀값은 코드에 적지 않는다. `read_secret(환경변수, 파일명)`은 환경변수를 먼저 보고, 없으면 `~/.openclaw/credentials/<파일명>`을 읽는다. 둘 다 없으면 로그에 이름을 남기고 그 단계에서 502를 돌려준다.

| 용도 | 환경변수 | 크리덴셜 파일 |
|---|---|---|
| Gateway | `OPENCLAW_GATEWAY_TOKEN` | `openclaw_gateway_token` (이미 있음) |
| Anthropic | `ANTHROPIC_API_KEY` | `anthropic_voice_api_key` (**없음**, 결정 필요) |
| STT | `STT_API_KEY` | `stt_api_key` (없음) |
| TTS | `GOOGLE_TTS_API_KEY` | `google_tts_api_key` (없음) |

## 수동 시험 순서 (승인 뒤)

```bash
PROFILE=home STATE_DIR=/tmp/ov python3 voice_bridge.py add-device sticks3-1   # 토큰 1회 출력
PROFILE=home BIND=192.168.1.50 STATE_DIR=/tmp/ov python3 voice_bridge.py
say -v Yuna -o /tmp/q.aiff "오늘 날씨 어때" && afconvert -f WAVE -d LEI16@16000 -c 1 /tmp/q.aiff /tmp/q.wav
curl -s -o /tmp/a.wav -D - -H "Authorization: Bearer <토큰>" -H "X-Device-Id: sticks3-1" \
     -H "Content-Type: audio/wav" --data-binary @/tmp/q.wav http://192.168.1.50:8765/v1/ask
afplay /tmp/a.wav
```

주의: `/tmp`는 재부팅하면 비워진다. 시험용으로만 쓴다.
