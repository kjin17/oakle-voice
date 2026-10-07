# M5Stack StickS3 (K150) 하드웨어 메모

조사일 2026-10-07 (같은 날 Muse SDK 대조로 일부 정정 — 정정한 칸은 「M」 출처와 *2026-10-07 정정* 표시). 실기는 아직 도착 전이라 **실측은 0건**이다. 등급 표기:
- **[근거]**: 아래 출처에 그대로 적혀 있음
- **[추론]**: 출처끼리 맞춰 보거나 같은 계열 칩에서 미루어 본 것
- **미확인**: 찾지 못했음

## 출처 약칭

| 약칭 | URL |
|---|---|
| D | https://docs.m5stack.com/en/core/StickS3 (PinMap 표와 회로도 PDF 링크) |
| U | https://github.com/m5stack/M5Unified (master: `src/M5Unified.inl`, `src/utility/Power_Class.inl`, `src/utility/power/M5PM1_Class.inl`, `src/utility/m5unified_i2c_addr.hpp`) |
| G | https://github.com/m5stack/M5GFX `src/M5GFX.cpp` (StickS3 패널 초기화, 2919~2990행 부근) |
| X | https://github.com/78/xiaozhi-esp32/tree/main/main/boards/m5stack/stick-s3 (`config.h`, `m5stack_stick_s3.cc`) |
| E | https://github.com/thenexthop2025/sticks3-voice (커뮤니티 ESPHome 음성비서 YAML, 2026-09-23 push, ESPHome 2026.9.x 대상) |
| C | https://github.com/ccsmart/M5SS3-esphome (커뮤니티 템플릿과 `components/m5pm1`, 2026-04 push) |
| M | Meta Muse Gadget SDK `esp32/components/muse/boards/board_m5stack_sticks3.c`, `esp32/devices/sdkconfig.muse-m5stack-sticks3`, `esp32/devices/README.md` (https://github.com/facebookincubator/muse-gadget-sdk, 커밋 b139b45, 2026-10-05). 주석에 「핀은 M5 K150 회로도 v0.6, 레지스터 비트는 UiFlow2에서 되읽음」이라 적혀 있다. 참고로만 읽었고 코드는 옮기지 않았다 |
| S | 회로도 https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/K150_Stick_S3_PRJ_V0.6_20251111_2025_11_17_16_10_24.pdf (**열어 보지 않음**) |

## 가장 중요한 함정 하나

**스피커 앰프(AW8737)의 enable 핀이 ESP32 GPIO에 없다.** 앰프는 PMIC(M5PM1, I2C 0x6E)의 G3에 붙어 있다. 그래서 I2C로 PMIC 레지스터를 써야 소리가 난다. LCD 전원(L3B)도 같은 식으로 M5PM1 G2에 달려 있다. *2026-10-07 정정:* L3B 는 LCD 만이 아니라 **오디오 쪽(코덱) 전원이고 코덱을 I2C 에 잇는 선**이기도 하다 [근거: M]. L3B 를 끄면 ES8311 이 I2C 에서 사라진다 [추론]. [근거: D, U, X(`AUDIO_CODEC_GPIO_PA` = NC, "PA control via PM1_G3"), E(on_boot lambda)]
→ AtomS3R이나 CoreS3 설정을 그대로 가져오면 앰프가 꺼진 채로 남는다. 증상은 "소리가 안 남"이다. [추론]

**두 번째 함정 (시험 판정을 속인다):** PMIC 는 ESP 가 리셋돼도 자기 GPIO 상태를 유지한다. 그래서 UiFlow2 를 막 지운 기기는 우리 초기화가 틀려도 소리가 난다. PMIC 자체가 리셋(완전 전원 차단)된 뒤에야 고장이 드러난다 [근거: M 주석]. → 앰프 시험은 **전원 버튼 두 번(완전 끄기) 후 다시 켠 상태**에서 한다 [추론].

## 핀맵

| 기능 | 값 | 출처 | 등급 |
|---|---|---|---|
| SoC | ESP32-S3-PICO-1-N8R8 (Flash 8MB, PSRAM 8MB) | D, 판매 페이지 | [근거] |
| PSRAM 모드 | Octal, 80 MHz | E, PlatformIO `qio_opi`, M(`CONFIG_SPIRAM_MODE_OCT`) | [근거] *2026-10-07 정정* (CNX 기사의 QSPI는 틀림) |
| 32 kHz 크리스털 | 없음 (라이트슬립 BLE 저전력 클럭을 메인 크리스털로 잡아야 함) | M sdkconfig 주석 | [근거] |
| I2S MCLK | GPIO18 | D, U, X, E | [근거] |
| I2S BCLK | GPIO17 | D, U, X, E | [근거] |
| I2S LRCK/WS | GPIO15 | D, U, X, E | [근거] |
| I2S DOUT (ESP → 코덱 → 스피커) | GPIO14 | D, U, X, E | [근거] |
| I2S DIN (코덱 → ESP, 마이크) | GPIO16 | D, U, X, E | [근거] |
| 코덱 | ES8311, I2C 0x18 | D, U(`es8311_i2c_addr0 = 0x18`), E | [근거] |
| 마이크 | 아날로그 MEMS → ES8311 ADC (MIC1P/N). PDM 아님 | U `_microphone_enabled_cb_sticks3`, E(`pdm: false`, `adc_type: external`) | [근거] |
| 내부 I2C | SDA GPIO47 / SCL GPIO48 (ES8311·BMI270·M5PM1 공유) | D, U, X, E | [근거] |
| 앰프 | AW8737. enable = M5PM1 G3. 레지스터: 0x16 bit3=0 GPIO 기능, 0x10 bit3=1 출력, **0x13 bit3=0 push-pull** (1이 open-drain 이고 PMIC 기본값), 0x11 bit3=1 켜기. enable 선에 풀다운이 있어 open-drain 으로 두면 **소리가 안 난다**. M 은 0x16 을 건드리지 않는다 | D, U, X, E, M | [근거] *2026-10-07 정정: 0x13 의미를 명확히* |
| LCD·오디오 전원 | M5PM1 G2 (L3B_EN) High. LCD 와 코덱 쪽 전원 둘 다. 0x13 bit2=0, 0x10 bit2=1, 0x11 bit2=1 | G, D, E, M | [근거] *2026-10-07 정정: 오디오 포함* |
| PMIC 깨우기 | M5PM1 reg 0x09 = 0x00 (I2C idle sleep 해제) | G | [근거] |
| LCD | ST7789P3, 135×240, SPI 40MHz. MOSI 39 / SCK 40 / DC 45 / CS 41 / RST 21, invert, offset x52 y40 | D, G, X, E | [근거] |
| 백라이트 | GPIO38 (PWM) | D, G, X, E | [근거] |
| 버튼 KEY1 (정면 A) / KEY2 (옆 B) | GPIO11 / GPIO12, active-low | D, U, X | [근거] |
| 전원 버튼 | M5PM1이 처리(한 번=켜기/리셋, 두 번=끄기, 길게=다운로드). ESP에서 읽을 GPIO 없음 | D | 동작은 [근거], GPIO가 없다는 건 [추론] |
| 다운로드 모드 / 첫 플래시 | ⚠️ 출처가 갈린다. D 는 「길게=다운로드」. M 은 「BOOT(GPIO0)를 PMIC 가 쥐고 있어 BOOT 버튼이 없고, 출하 UiFlow2 가 ESP32-S3 USB 시리얼을 꺼 둬서 esptool 이 못 찾는다 → UiFlow2 REPL 에서 USB-Serial/JTAG 를 되살리는 레지스터 4줄을 쓴 뒤 `--after no-reset` 로 굽는다」. 공통: **굽기 전에 8MB 전체를 `read-flash 0 0x800000` 으로 백업** | D, M(`devices/README.md`) | 둘 다 [근거], 어느 쪽이 실기에서 되는지는 미확인 |
| 딥슬립 깨우기 | KEY1/KEY2 를 ext1 ANY_LOW 로. 버튼 풀업이 딥슬립에도 살아 있는 레일에 있다. 깨운 버튼은 부팅 때 아직 눌려 있다 | M | [근거] / 실기 미확인 |
| 배터리 전압 | M5PM1 reg 0x22/0x23, **16bit 리틀엔디언 mV** | U(`getBatteryVoltage`: `(buf[1]<<8)\|buf[0]`), E, M | [근거] *2026-10-07 정정: 12bit → 16bit* |
| USB 입력 전압 | M5PM1 reg 0x24/0x25, 16bit LE mV. 4000 mV 넘으면 USB 연결로 본다 | U(`M5PM1_REG_VIN_L`), M | [근거] |
| 충전 중 | M5PM1 G0 입력 Low (= reg 0x12 bit0 == 0). 충전 IC 의 CHRG 핀. M 은 「USB 있음 && bit0==0」 을 충전으로 본다 | U, X, D, M | [근거] |
| IMU | BMI270 @0x68, INT는 M5PM1 G4 | D, U | [근거] |
| IR | TX GPIO46 / RX GPIO42. 수신은 RMT 필요, 수신 중엔 앰프 off, EXT_5V 필요 | D | [근거] |
| EXT_5V | `M5.Power.setExtOutput()` (M5PM1). 기본값 꺼짐 | D, U | [근거] |
| Grove PORT.A | SDA GPIO9 / SCL GPIO10 | D, U | [근거] |
| Grove·IR 5V | M5PM1 0x06 bit3 = 5V 출력 (Grove 와 IR 용). 기본 꺼짐으로 두면 된다 | M, 포럼 https://community.home-assistant.io/t/m5stack-sticks3-grove-i-c-issue-with-scd41-works-on-atoms3u/1012075 | [근거] *2026-10-07: 포럼 [추론] → M 으로 확인* |
| PMIC 녹색 LED | M5PM1 0x06 bit4. **기본으로 늘 켜져 1 mA 남짓**을 먹는다. 끄면 대기 전류가 준다 | M | [근거] (전류값은 M 주석, 실측 아님) |
| Hat2-Bus 16P | G5, G4, G6, G1, G7, G8, G43, G44, G2, G3, Boot, BAT, EXT_5V, 5V_IN, 3V3_L2, GND | D | [근거] |
| ES8311 스피커 초기화 | 0x01=0xB5, 0x02=0x18, 0x0D=0x01, 0x12=0x00, 0x13=0x10, 0x32=0xBF, 0x37=0x08 | U | [근거] |
| ES8311 ADC 재무장 | 0x14=0x1A, 0x0E=0x02, 0x0D=0x01 (I2S RX 클럭이 돈 뒤에 다시 쓰기) | E `arm_es8311_adc` | [근거] (커뮤니티 설정) |
| ES8311 초기화 순서 | M 은 I2S 를 먼저 켜서 MCLK 를 돌린 뒤 코덱 드라이버(esp_codec_dev)를 붙인다. E 의 「클럭 뒤 재무장」과 같은 방향이다 | M `audio_init`, E | [근거] |
| 마이크 게인 | **M5Unified 와 Muse 가 다르다.** U: 0x14=0x10(아날로그 PGA 최소) + 0x17=0xFF(디지털 볼륨 최대). M: 아날로그 입력 게인 기본 30 dB(설정에서 0~36). E 의 0x14=0x1A 도 PGA 30 dB 다 | U `_microphone_enabled_cb_sticks3`, M `muse_settings.c`, E | [근거] / 음질 차이는 [추론] → 벤치마크 항목 |
| 오디오 형식 | U: 스피커 22050 Hz 스테레오(I2S0), 마이크 I2S1. M: 16 kHz 16bit 스테레오 슬롯, 마이크는 왼쪽 슬롯 하나 | U, M | [근거] |

## 미확인

- ~~사용자 LED~~ → *2026-10-07 해결*: PMIC 녹색 LED 이고 0x06 bit4 로 켜고 끈다 [근거: M]. ESP GPIO 에 달린 LED 는 없다 [추론].
- **ESP32 GPIO13**: D의 M5PM1 표에 나오지만 용도가 애매하다. PM1→ESP 인터럽트나 wake 선으로 보인다. [추론] 확정하려면 회로도 S를 봐야 한다.
- **전이중(동시 녹음+재생)**: M5Unified는 스피커를 I2S_NUM_0, 마이크를 I2S_NUM_1에 두고 같은 핀을 번갈아 쓴다. [근거: U] *2026-10-07 보충:* Muse 는 I2S_NUM_0 하나에 TX·RX 를 같이 열고 ES8311 을 양방향(BOTH)으로 써서 **전이중으로 돈다** [근거: M]. 그러니 하드웨어는 전이중이 된다 [추론]. 버튼 눌러 말하기는 반이중이라 지금은 필요 없고, 재생 중 끼어들기(바지인)나 에코 제거가 필요해지면 그때 ESP-IDF 로 간다 [추론].
- **AW8737 모드 펄스**: 이름이 "SPK_Pulse"라 펄스로 모드를 고르는 칩으로 보이지만, 소스는 단순 High/Low로만 쓴다. [추론]
- **실제 배터리 지속시간**: 250mAh에서 Wi-Fi를 켠 채 대기하면 얼마나 가는지 출처가 없다. → 도착 후 실측해야 한다.

## ESPHome / 펌웨어 생태계

| 항목 | 상태 | 출처 | 등급 |
|---|---|---|---|
| devices.esphome.io의 StickS3 페이지 | 없음 (`/devices/m5stack-sticks3` → 404) | 2026-10-07 접속 | [근거] |
| m5stack/esphome-yaml의 StickS3 | 없음. atom-echos3r, atoms3r-echo-base만 있음 | https://github.com/m5stack/esphome-yaml | [근거] |
| ESPHome 본가의 M5PM1 컴포넌트 | 없음. lambda로 PMIC 레지스터를 직접 쓰거나 외부 컴포넌트(C) 사용 | E, C | [근거] |
| 커뮤니티 음성비서 YAML | 있음 (E): voice_assistant, micro_wake_word(hey_jarvis), es8311 audio_dac, st7789v 디스플레이 | E | [근거] |
| 포팅 기준 | `m5stack/esphome-yaml/examples/voice_assistant/atom-echos3r.factory.yaml`. 같은 ES8311+I2S 구성이고 핀과 PMIC만 다름 | https://github.com/m5stack/esphome-yaml | [추론] |
| xiaozhi-esp32 공식 보드 | 있음 (`boards/m5stack/stick-s3`, 24kHz) | X, https://docs.m5stack.com/en/guide/sticks3/xiaozhi_voice_assistant | [근거] |
| M5Unified 지원 | `board_M5StickS3` 분기 있음. `M5.Mic`과 `M5.Speaker`가 핀·PMIC·앰프를 알아서 잡아 준다 | U | [근거] (분기는 있음) / 실제 동작은 [추론] |
| Zephyr 비공식 포트 | 핀 정보가 위 표와 같음 | https://github.com/thc1006/zephyr-m5stack-sticks3 | [근거] |

## 펌웨어 설계에 주는 함의 [추론]

1. 커스텀 펌웨어는 **M5Unified를 쓰면 핀을 직접 다룰 필요가 거의 없다.** `M5.begin()`이 PMIC, 앰프, LCD 전원을 처리한다. 그래서 `firmware/` 초안은 핀 상수 없이 M5Unified API만 쓴다.
2. ESPHome으로 가면 PMIC lambda(E의 on_boot)와 ADC 재무장 스크립트가 꼭 필요하다. `esphome/sticks3-v1.yaml`이 E를 근거로 그 둘을 옮겨 왔다.
3. 마이크 녹음 중에는 스피커를 끄고, 재생 중에는 마이크를 끈다(반이중). M5Unified에서는 `M5.Mic.begin()`/`end()`와 `M5.Speaker.begin()`/`end()`를 번갈아 부른다.
