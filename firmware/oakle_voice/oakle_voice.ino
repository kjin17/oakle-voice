// oakle-voice 커스텀 펌웨어 초안 — 경로 A (v1 집 안 / v2 바깥 공통)
// ⚠️ 컴파일·실기 검증 0회. Arduino-ESP32 3.x + M5Unified 최신판 가정 [추론].
//
// 동작: A 버튼을 누르는 동안 녹음(16 kHz mono, 최대 MAX_SEC초, PSRAM)
//       → 떼면 WAV 를 POST /v1/ask → 받은 WAV 재생. 화면엔 상태, 받아 적은 말, 답 앞부분.
//       재생 중 A 를 다시 누르면 재생을 끊고 바로 새로 녹음한다.
// 핀은 직접 다루지 않는다. M5.begin() 이 StickS3 의 PMIC(M5PM1)·앰프·LCD 전원을 잡는다 [근거: M5Unified board_M5StickS3 분기].
// 예외: 절전용으로 PMIC 레지스터 몇 개를 직접 쓴다(pmic_bit). 값은 docs/sticks3-hardware.md 표.
// 마이크와 스피커는 같은 I2S 핀을 번갈아 쓴다 → 반드시 한쪽을 end() 한 뒤 다른 쪽 begin() [근거: M5Unified 구조].
//
// 시리얼에는 질의마다 한 줄씩 단계별 지연을 남긴다(벤치마크용, docs/muse-sdk-benchmark.md §4):
//   timing ms: held=… post=+… hdr=+… body=+… play=+… | server stt=…;llm=…;tts=…
//   (+값은 버튼을 뗀 순간 기준)
//
// 설정: config.h.example 을 config.h 로 복사해 채운다 (config.h 는 git 제외).
//
// 표정: 화면 왼쪽 120×120 에 오클 표정, 오른쪽에 글자. 그림 데이터 faces/faces_data.h 는 리포에 없다
//       (tools/make_face_assets.py 로 만든다, faces/README.md). 없으면 도형 얼굴로 대신 그린다.
//       표정 이름은 오클 사무실 docs/emotion.md 의 20종(joy … mindblown)과 같다.

#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "esp_sleep.h"
#include "config.h"   // WIFI_SSID, WIFI_PASS, SERVER_URL, DEVICE_ID, DEVICE_TOKEN, (USE_TLS, ROOT_CA)
#if __has_include("faces/faces_data.h")
#include "faces/faces_data.h"  // FACE_TABLE[FACE_COUNT] = {이름, RGB565 120×120}
#define HAVE_FACES 1
#else
#define HAVE_FACES 0
#endif

// ---- 조절값 (config.h 에서 덮어쓸 수 있다) ----
#ifndef MAX_SEC
#define MAX_SEC 10
#endif
#ifndef TAIL_TRIM_MS
#define TAIL_TRIM_MS 120         // 끝부분을 버린다: 버튼 떼는 딸깍 소리가 STT 에 「.」나 잡음 단어로 들어가지 않게
#endif
#ifndef MIN_HOLD_MS
#define MIN_HOLD_MS 400          // 이보다 짧게 누른 건 말이 아니라 실수로 본다
#endif
#ifndef DIM_AFTER_S
#define DIM_AFTER_S 20           // 손 안 대면 화면을 어둡게
#endif
#ifndef SLEEP_AFTER_S
#define SLEEP_AFTER_S 300        // 배터리일 때 이만큼 놀면 딥슬립. 0 = 끔. 버튼 A/B 로 깬다
#endif
#ifndef MIC_PGA_DB
#define MIC_PGA_DB -1            // -1 = M5Unified 기본(아날로그 PGA 최소 + 디지털 볼륨 최대). 0~30(3 dB 단위)이면 아날로그로 키우고 디지털은 0 dB.
                                 //  Muse 기본은 아날로그 30 dB [근거: Muse SDK muse_settings.c]. 어느 쪽이 나은지는 §4 벤치마크로 정한다
#endif
#ifndef PMIC_LED_OFF
#define PMIC_LED_OFF 1           // PMIC 녹색 LED 는 기본으로 늘 켜져 1 mA 남짓 먹는다 [근거: Muse SDK 보드 코드 주석]
#endif

static const uint32_t RATE = 16000;
static const size_t CHUNK = 1600;                 // 100 ms
static int16_t* pcm = nullptr;                    // PSRAM
static size_t pcm_len = 0;                        // samples
static uint32_t t_press, t_release;               // millis
static uint32_t last_touch;                       // 마지막으로 사람이 손댄 시각
static bool dimmed = false;
static uint32_t wifi_next_try = 0, wifi_backoff = 1000;

// ---- PMIC(M5PM1, 0x6E) 직접 쓰기: 절전에만 쓴다 ----
// PMIC 는 작은 MCU 라 I2C 를 가끔 놓친다 → 세 번까지 다시 한다 [근거: Muse SDK board_m5stack_sticks3.c 주석] / 실기 미확인
static const uint8_t PM1 = 0x6E;
static const uint8_t PM1_PWR_CFG = 0x06;          // bit3 = 5V 출력, bit4 = 녹색 LED
static const uint8_t PM1_GPIO_OUT = 0x11;         // bit2 = L3B(LCD+오디오 전원), bit3 = 앰프
static const uint8_t PM1_VIN_MV_L = 0x24;         // USB 입력 전압, 16bit LE mV

static bool pmic_bit(uint8_t reg, uint8_t mask, bool on) {
  for (int i = 0; i < 3; i++) {
    bool ok = on ? M5.In_I2C.bitOn(PM1, reg, mask, 100000) : M5.In_I2C.bitOff(PM1, reg, mask, 100000);
    if (ok) return true;
    delay(2);
  }
  return false;
}

static bool on_usb() {
  uint8_t b[2] = {0, 0};
  if (!M5.In_I2C.readRegister(PM1, PM1_VIN_MV_L, b, 2, 100000)) return true;   // 못 읽으면 잠들지 않는 쪽으로
  return (b[0] | (b[1] << 8)) > 4000;
}

// ---- 화면: 왼쪽 표정(FACE_X..+120), 오른쪽 글자(TX..240) ----
static const int FACE_X = 0, FACE_Y = 8, FACE_S = 120;
static const int TX = 124, TW = 240 - TX - 2;

// 상태별 표정. 이름은 사무실 docs/emotion.md 와 같다. 대답 표정은 서버가 X-Oakle-Emotion 으로 준다
#define FACE_BOOT   "hello"      // 켜짐·Wi-Fi 잡는 중
#define FACE_IDLE   "joy"        // 대기
#define FACE_LISTEN "yes"        // 듣는 중 (경례)
#define FACE_THINK  "curious"    // 보내고 기다리는 중
#define FACE_SPEAK  "ok"         // 대답 표정이 없거나 모르는 이름일 때
#define FACE_ERROR  "surprised"  // 서버·메모리 오류
#define FACE_NOWIFI "mindblown"  // 연결 없음
#define FACE_HUH    "curious"    // 너무 짧음·말소리 없음
#define FACE_SLEEPY "sleepy"     // 오래 쉼(화면 어둡게)

#if !HAVE_FACES
// 도형 얼굴: 표정 그림이 없을 때(공개 리포 그대로 빌드) 대신 그린다
static void shape_face(const char* f) {
  auto& d = M5.Display;
  const int cx = FACE_X + FACE_S / 2, cy = FACE_Y + FACE_S / 2 + 6, r = 44;
  d.fillRect(FACE_X, FACE_Y, FACE_S, FACE_S, TFT_BLACK);
  d.drawLine(cx, cy - r, cx, cy - r - 10, TFT_LIGHTGREY);                // 안테나
  d.fillCircle(cx, cy - r - 13, 4, TFT_ORANGE);
  d.fillCircle(cx, cy, r, 0xE71C);                                       // 헬멧(밝은 회색)
  d.fillCircle(cx, cy + 4, r - 10, 0xFF9B);                              // 얼굴(살구색)
  const int ex = 15, ey = cy - 2, my = cy + 18;
  auto is = [&](const char* n) { return strcmp(f, n) == 0; };
  if (is("sleepy") || is("joy") || is("lol") || is("thanks")) {          // 감은 눈 / 웃는 눈
    d.drawArc(cx - ex, ey + (is("sleepy") ? -3 : 3), 5, 4, is("sleepy") ? 20 : 200, is("sleepy") ? 160 : 340, TFT_BLACK);
    d.drawArc(cx + ex, ey + (is("sleepy") ? -3 : 3), 5, 4, is("sleepy") ? 20 : 200, is("sleepy") ? 160 : 340, TFT_BLACK);
  } else if (is("mindblown") || is("no")) {                              // X 눈
    for (int s : {-1, 1}) {
      d.drawLine(cx + s * ex - 4, ey - 4, cx + s * ex + 4, ey + 4, TFT_BLACK);
      d.drawLine(cx + s * ex - 4, ey + 4, cx + s * ex + 4, ey - 4, TFT_BLACK);
    }
  } else {
    int er = is("surprised") ? 6 : 4;
    d.fillCircle(cx - ex, ey, er, TFT_BLACK);
    d.fillCircle(cx + ex, ey, er, TFT_BLACK);
  }
  if (is("surprised") || is("curious") || is("yes")) d.fillCircle(cx, my, is("surprised") ? 6 : 3, TFT_BLACK);   // 동그란 입
  else if (is("sad") || is("gloomy") || is("sorry") || is("angry") || is("mindblown") || is("no"))
    d.drawArc(cx, my + 10, 9, 8, 220, 320, TFT_BLACK);                    // 처진 입
  else if (is("sleepy")) d.drawLine(cx - 4, my, cx + 4, my, TFT_BLACK);
  else d.drawArc(cx, my - 6, 10, 9, 30, 150, TFT_BLACK);                   // 웃는 입
  if (is("shy") || is("love") || is("joy")) {                             // 볼
    d.fillCircle(cx - 26, cy + 10, 5, 0xFB2C);
    d.fillCircle(cx + 26, cy + 10, 5, 0xFB2C);
  }
}
#endif

static void draw_face(const char* f) {
#if HAVE_FACES
  for (size_t i = 0; i < FACE_COUNT; i++)
    if (strcmp(FACE_TABLE[i].name, f) == 0) {
      M5.Display.pushImage(FACE_X, FACE_Y, FACE_W, FACE_H, FACE_TABLE[i].px);
      return;
    }
  for (size_t i = 0; i < FACE_COUNT; i++)                                // 모르는 이름이면 OK 얼굴
    if (strcmp(FACE_TABLE[i].name, FACE_SPEAK) == 0) { M5.Display.pushImage(FACE_X, FACE_Y, FACE_W, FACE_H, FACE_TABLE[i].px); return; }
#else
  shape_face(f);
#endif
}

static void touch() {
  last_touch = millis();
  if (dimmed) { M5.Display.setBrightness(128); dimmed = false; }
}

static void status(const char* s, uint16_t color = TFT_WHITE, const char* face = FACE_IDLE) {
  touch();
  M5.Display.fillScreen(TFT_BLACK);
  draw_face(face);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextColor(color);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(TX, 6);
  M5.Display.print(s);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(TX, 125);
  M5.Display.printf("%d%% %s", M5.Power.getBatteryLevel(), WiFi.isConnected() ? "wifi" : "no wifi");
}

// 오른쪽 칸에 한글을 글자 단위로 줄바꿈해 찍는다(라이브러리 줄바꿈은 화면 왼쪽 끝으로 돌아간다 [추론]).
// efontKR 은 M5GFX(LovyanGFX)에 들어 있는 한글 글꼴이다 [추론: 실기에서 글꼴 이름 확인 필요]. 넘치는 줄은 버린다.
static int caption(int y, const String& s, uint16_t color, int max_y = 120) {
  auto& d = M5.Display;
  d.setFont(&fonts::efontKR_12);
  d.setTextSize(1);
  d.setTextColor(color, TFT_BLACK);
  d.setTextWrap(false);
  const int lh = d.fontHeight() + 1;
  int x = TX;
  for (size_t i = 0; i < s.length() && y + lh <= max_y;) {
    uint8_t c = s[i];
    size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;   // UTF-8 한 글자
    String ch = s.substring(i, i + n);
    i += n;
    if (ch == "\n") { x = TX; y += lh; continue; }
    int w = d.textWidth(ch);
    if (x + w > TX + TW) { x = TX; y += lh; if (y + lh > max_y) break; if (ch == " ") continue; }
    d.setCursor(x, y);
    d.print(ch);
    x += w;
  }
  d.setFont(&fonts::Font0);
  return y + lh;
}

static String url_decode(const String& in) {
  String out; out.reserve(in.length());
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '%' && i + 2 < in.length()) {
      out += (char)strtol(in.substring(i + 1, i + 3).c_str(), nullptr, 16); i += 2;
    } else out += (c == '+') ? ' ' : c;
  }
  return out;
}

// 녹음 중 입력 크기 막대(0~1). 말이 들어가고 있는지 눈으로 확인하는 용도.
static void level_bar(const int16_t* p, size_t n) {
  int peak = 0;
  for (size_t i = 0; i < n; i++) { int v = abs(p[i]); if (v > peak) peak = v; }
  int w = (int)((long)TW * peak / 32768);
  M5.Display.fillRect(TX, 100, TW, 10, TFT_DARKGREY);
  M5.Display.fillRect(TX, 100, w, 10, peak > 30000 ? TFT_RED : TFT_GREEN);   // 빨강 = 잘림
}

// ---- WAV ----
static void put_le(uint8_t* p, uint32_t v, int n) { for (int i = 0; i < n; i++) p[i] = (v >> (8 * i)) & 0xFF; }

static void wav_header(uint8_t* h, uint32_t data_bytes) {
  memcpy(h, "RIFF", 4); put_le(h + 4, 36 + data_bytes, 4); memcpy(h + 8, "WAVEfmt ", 8);
  put_le(h + 16, 16, 4); put_le(h + 20, 1, 2); put_le(h + 22, 1, 2); put_le(h + 24, RATE, 4);
  put_le(h + 28, RATE * 2, 4); put_le(h + 32, 2, 2); put_le(h + 34, 16, 2);
  memcpy(h + 36, "data", 4); put_le(h + 40, data_bytes, 4);
}

// ---- 녹음 ----
static void record_while_held() {
  t_press = millis();
  M5.Speaker.end();
  M5.Mic.begin();
  pcm_len = 0;
#if MIC_PGA_DB >= 0
  // ES8311(0x18) reg 0x14: bit4 = MIC1 선택, bit3..0 = PGA 3 dB 단위 / reg 0x17: ADC 디지털 볼륨, 0xBF = 0 dB.
  // M5Unified 의 마이크 콜백이 begin 때 0x14=0x10, 0x17=0xFF 를 쓰므로 그 뒤에 덮어쓴다 [근거: M5Unified.inl _microphone_enabled_cb_sticks3]
  // 콜백이 첫 record 때 다시 불리면 덮어쓴 값이 사라진다 → 실기에서 레지스터를 읽어 확인할 것 [추론]
  M5.In_I2C.writeRegister8(0x18, 0x14, 0x10 | (MIC_PGA_DB / 3), 100000);
  M5.In_I2C.writeRegister8(0x18, 0x17, 0xBF, 100000);
#endif
  status("LISTEN", TFT_GREEN, FACE_LISTEN);
  const size_t max_samples = RATE * MAX_SEC;
  while (M5.BtnA.isPressed() && pcm_len + CHUNK <= max_samples) {
    if (M5.Mic.record(pcm + pcm_len, CHUNK, RATE)) {
      if (pcm_len >= CHUNK) level_bar(pcm + pcm_len - CHUNK, CHUNK);   // 직전 조각은 채워져 있다 [추론: record 는 비동기]
      pcm_len += CHUNK;
    }
    M5.update();
  }
  t_release = millis();
  while (M5.Mic.isRecording()) delay(1);
  M5.Mic.end();
  size_t trim = RATE * TAIL_TRIM_MS / 1000;
  pcm_len = pcm_len > trim ? pcm_len - trim : 0;
}

// ---- 질의와 재생. 재생 중 A 를 다시 누르면 true (바로 새 녹음) ----
static bool ask_and_play() {
  if (t_release - t_press < MIN_HOLD_MS || pcm_len < RATE / 4) { status("short", TFT_WHITE, FACE_HUH); return false; }
  if (!WiFi.isConnected()) { status("NOWIFI", TFT_RED, FACE_NOWIFI); return false; }
  status("SEND", TFT_YELLOW, FACE_THINK);
  uint32_t data_bytes = pcm_len * 2;
  uint8_t* body = (uint8_t*)ps_malloc(44 + data_bytes);
  if (!body) { status("NOMEM", TFT_RED, FACE_ERROR); return false; }
  wav_header(body, data_bytes);
  memcpy(body + 44, pcm, data_bytes);

  HTTPClient http;
#if USE_TLS
  WiFiClientSecure tls;
  tls.setCACert(ROOT_CA);
  http.begin(tls, SERVER_URL);
#else
  http.begin(SERVER_URL);
#endif
  http.setTimeout(120000);                       // OpenClaw 에이전트 턴은 수십 초 걸릴 수 있다 [추론]
  http.addHeader("Authorization", String("Bearer ") + DEVICE_TOKEN);
  http.addHeader("X-Device-Id", DEVICE_ID);
  http.addHeader("Content-Type", "audio/wav");
  const char* want[] = {"X-Transcript", "X-Answer", "X-Timing", "X-Oakle-Emotion"};
  http.collectHeaders(want, 4);
  uint32_t t_post = millis();
  status("THINK", TFT_YELLOW, FACE_THINK);
  int code = http.POST(body, 44 + data_bytes);   // 서버가 STT·LLM·TTS 를 다 끝내야 돌아온다
  uint32_t t_hdr = millis();
  free(body);

  if (code == 204) { status("no speech", TFT_WHITE, FACE_HUH); http.end(); return false; }
  if (code != 200) {
    char msg[24]; snprintf(msg, sizeof msg, "ERR %d", code);
    status(msg, TFT_RED, code < 0 ? FACE_NOWIFI : FACE_ERROR);   // 502 본문 = 실패 단계(stt/llm/tts), 음수 = 연결 실패
    M5.Display.setCursor(TX, 40); M5.Display.print(code < 0 ? http.errorToString(code) : http.getString().substring(0, 20));
    Serial.printf("ask failed: code=%d after %lu ms\n", code, (unsigned long)(t_hdr - t_release));
    http.end(); return false;
  }
  String heard = url_decode(http.header("X-Transcript"));
  String answer = url_decode(http.header("X-Answer"));
  String srv = http.header("X-Timing");
  String emo = http.header("X-Oakle-Emotion");     // 사무실 표정 이름(joy …). 비었거나 모르는 이름이면 OK 얼굴
  emo.trim();
  if (emo.length() == 0) emo = FACE_SPEAK;
  int len = http.getSize();
  if (len <= 44 || len > 2 * 1024 * 1024) { status("BADLEN", TFT_RED, FACE_ERROR); http.end(); return false; }
  uint8_t* wav = (uint8_t*)ps_malloc(len);
  if (!wav) { status("NOMEM", TFT_RED, FACE_ERROR); http.end(); return false; }
  WiFiClient* s = http.getStreamPtr();
  int got = 0; uint32_t t0 = millis();
  while (got < len && millis() - t0 < 30000) {
    int n = s->readBytes(wav + got, len - got);
    if (n > 0) got += n;
  }
  uint32_t t_body = millis();
  http.end();

  status("SPEAK", TFT_CYAN, emo.c_str());
  int y = caption(26, heard.substring(0, 60), TFT_DARKGREY, 52);   // 무엇으로 알아들었는지 먼저(두 줄까지)
  caption(y + 2, answer, TFT_WHITE);
  M5.Speaker.begin();
  M5.Speaker.setVolume(180);
  M5.Speaker.playWav(wav, got);
  uint32_t t_play = millis();
  Serial.printf("timing ms: held=%lu post=+%lu hdr=+%lu body=+%lu play=+%lu | server %s\n",
                (unsigned long)(t_release - t_press), (unsigned long)(t_post - t_release),
                (unsigned long)(t_hdr - t_release), (unsigned long)(t_body - t_release),
                (unsigned long)(t_play - t_release), srv.c_str());
  bool barge = false;
  while (M5.Speaker.isPlaying()) {
    M5.update();
    if (M5.BtnA.wasPressed()) { M5.Speaker.stop(); barge = true; break; }
    delay(10);
  }
  free(wav);
  if (!barge) status("READY");
  return barge;
}

// ---- 전원 ----
static void deep_sleep() {
  Serial.println("idle: deep sleep (wake: A or B)");
  M5.Display.setBrightness(0);
  M5.Display.sleep();
  WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
  pmic_bit(PM1_GPIO_OUT, 0x08 | 0x04, false);    // 앰프와 L3B(LCD·코덱 전원)를 끈다
  while (digitalRead(GPIO_NUM_11) == LOW || digitalRead(GPIO_NUM_12) == LOW) delay(20);   // 누른 채로 잠들면 바로 깬다
  delay(50);
  // KEY1/KEY2 는 active-low, 풀업은 딥슬립에도 살아 있는 쪽에 있다 [근거: Muse SDK 보드 코드 주석] / 실기 미확인
  esp_sleep_enable_ext1_wakeup((1ULL << GPIO_NUM_11) | (1ULL << GPIO_NUM_12), ESP_EXT1_WAKEUP_ANY_LOW);
  esp_deep_sleep_start();                        // 깨면 setup() 부터 다시 (Wi-Fi 재접속 2~3초 [추론])
}

static void keep_wifi() {
  if (WiFi.isConnected()) { wifi_backoff = 1000; return; }
  if ((int32_t)(millis() - wifi_next_try) < 0) return;
  WiFi.reconnect();
  wifi_next_try = millis() + wifi_backoff;
  wifi_backoff = min<uint32_t>(wifi_backoff * 2, 60000);   // 1초 → 최대 60초
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  M5.Display.setRotation(1);
#if PMIC_LED_OFF
  pmic_bit(PM1_PWR_CFG, 0x10, false);
#endif
  pcm = (int16_t*)ps_malloc(RATE * MAX_SEC * 2);
  status("WIFI..", TFT_WHITE, FACE_BOOT);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 60 && !WiFi.isConnected(); i++) delay(250);
  if (WiFi.isConnected()) status("READY");
  else status("NOWIFI", TFT_RED, FACE_NOWIFI);
}

void loop() {
  M5.update();
  keep_wifi();
  if (M5.BtnA.wasPressed()) {
    touch();
    do { record_while_held(); } while (ask_and_play());   // 재생 끊고 다시 말하기
  }
  if (M5.BtnB.wasClicked()) status("READY");       // TODO: 볼륨/프로필 전환
  uint32_t idle = millis() - last_touch;
  if (!dimmed && idle > DIM_AFTER_S * 1000UL) {   // 오래 쉼: 졸린 얼굴로 바꾸고 어둡게
    draw_face(FACE_SLEEPY);
    M5.Display.setBrightness(16); dimmed = true;
  }
  if (SLEEP_AFTER_S && idle > SLEEP_AFTER_S * 1000UL && !on_usb()) deep_sleep();
  delay(10);
}
