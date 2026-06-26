#pragma once
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"

// ============================================================================
//  VIVO DESK-V110EW ESPHome external component  (HARDWARE UART version)
//
//  Ported from ESP8266/SoftwareSerial → ESP32-C3 hardware UARTs for clean,
//  jitter-free signaling (the bit-banged SoftwareSerial was the suspected
//  reason the brain accepted our heartbeat but ignored our MOVE commands).
//  Backup of the ESP8266 version: vivo_desk.h.esp8266-softwareserial.bak
//
//  Two hardware UART buses are passed in from YAML (each 19200 8N1):
//    hand_uart  : RX ← Handset TX,  TX → Handset RX
//    brain_uart : RX ← Brain TX,    TX → Brain RX
//
//  Protocol: ASCII, 19200 baud, `:cmd data checksum;`
//  Checksum: sum of cmd+data bytes mod 256 as 2 uppercase hex ASCII chars
//
//  Usage from YAML lambda:
//    id(desk).start_up();  / start_down() / stop() / goto_preset(1..4)
// ============================================================================

namespace esphome {
namespace vivo_desk {

class VivoDeskComponent : public Component {
 public:
  void set_height_sensor(sensor::Sensor *s) { height_ = s; }
  void set_hand_uart(uart::UARTComponent *u) { hand_uart_ = u; }
  void set_brain_uart(uart::UARTComponent *u) { brain_uart_ = u; }

  void start_up()            { move_dir_ = UP;   last_inject_ = 0; ESP_LOGI("vivo_desk", "MOVE UP start"); }
  void start_down()          { move_dir_ = DOWN; last_inject_ = 0; ESP_LOGI("vivo_desk", "MOVE DOWN start"); }
  void stop()                { move_dir_ = NONE; preset_active_ = false; ESP_LOGI("vivo_desk", "MOVE stop"); }
  void goto_preset(uint8_t n) {
    if (n < 1 || n > 4) return;
    ESP_LOGI("vivo_desk", "GOTO preset %u", n);
    move_dir_ = NONE;                         // a preset overrides any held direction
    preset_m_active_ = preset_m_[n-1];
    preset_b_active_ = preset_b_[n-1];
    preset_active_ = true;                     // start the faithful replay (serviced in loop)
    preset_t0_ = millis();
    preset_last_slot_ = -1;
  }

  // Runtime relay toggle (transparent MITM handset<->brain). Default OFF (= emulate mode).
  void set_relay(bool on) { relay_enabled_ = on; ESP_LOGI("vivo_desk", "relay %s", on ? "ON" : "OFF"); }
  void set_verbose(bool on) { verbose_ = on; ESP_LOGI("vivo_desk", "verbose %s", on ? "ON" : "OFF"); }
  void set_ack(bool on) { ack_on_height_ = on; ESP_LOGI("vivo_desk", "ack_on_height %s", on ? "ON" : "OFF"); }
  void set_spoof(bool on) { spoof_handset_ = on; ESP_LOGI("vivo_desk", "spoof_handset %s", on ? "ON" : "OFF"); }
  // Emulate the real handset toward the brain (default operating mode): version + acks + heartbeat.
  void set_emulate(bool on) { emulate_enabled_ = on; ESP_LOGI("vivo_desk", "emulate %s", on ? "ON" : "OFF"); }
  // Pure-listen diagnostic / sniffer: suppress ALL TX, only RX+log both lines.
  void set_silent(bool on) { silent_listen_ = on; ESP_LOGI("vivo_desk", "silent_listen %s", on ? "ON" : "OFF"); }

  void setup() override {
    ESP_LOGI("vivo_desk", "Started (HW UART) — emulate=%s relay=%s",
             emulate_enabled_ ? "ON" : "OFF", relay_enabled_ ? "ON" : "OFF");
  }

  void loop() override {
    relay_();             // forwards BOTH directions immediately (relay on) + parses height
    if (!silent_listen_) {
      if (relay_enabled_) {
        drive_ha_();         // HA up/down/preset: act as the handset, relay suppressed meanwhile
      } else {
        emulate_handset_();  // standalone: ESP *is* the handset (no real handset present)
        spoof_height_();
        poll_brain_();
      }
    }
    log_stats_();
  }

  void dump_config() override {
    ESP_LOGCONFIG("vivo_desk", "VIVO Desk (HW UART):");
    ESP_LOGCONFIG("vivo_desk", "  hand_uart + brain_uart @ 19200 8N1");
    if (height_) ESP_LOGCONFIG("vivo_desk", "  Height sensor configured");
  }

  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  // ---- Hardware UART buses (set from YAML) --------------------------------
  uart::UARTComponent *hand_uart_{nullptr};
  uart::UARTComponent *brain_uart_{nullptr};

  sensor::Sensor *height_{nullptr};

  // ---- Behaviour flags -----------------------------------------------------
  bool relay_enabled_{false};   // OFF = ESP is the controller (emulate handset toward brain)
  bool verbose_{true};          // per-frame hex logging
  bool ack_on_height_{true};    // reply :A41; to brain after each valid height frame
  bool spoof_handset_{false};   // stream fake height to handset, capture its ack
  bool silent_listen_{false};   // diagnostic/sniffer: suppress ALL TX, pure RX listen on both lines

  // ---- Movement state -----------------------------------------------------
  enum MoveDir : uint8_t { NONE, UP, DOWN } move_dir_{NONE};
  uint32_t last_inject_{0};
  static constexpr uint32_t INJECT_INTERVAL_MS = 150;

  // ---- HA preset recall window --------------------------------------------
  // A real handset press STOPS the heartbeat and repeats the command frames for the
  // duration of the press. To make HA presets work in relay mode we mimic that: hold a
  // "preset window" during which we (a) suppress the handset->brain relay forward and
  // (b) repeat the preset M+B pair, so the brain sees a clean, sustained command.
  // Preset recall replays the EXACT captured handset tap as a faithful frame stream. From the
  // capture (M1): M, idle, B, ~15 idles, M, idle, B, then idle while the brain auto-drives —
  // i.e. a "wake" tap then an "action" tap. CRITICAL: while replaying we SUPPRESS the relayed
  // :A41; idle stream and emit the WHOLE stream ourselves (K-frames AND the :A41; idles), so our
  // command isn't garbled by the handset's parallel :A41; (which caused the brain's relay to
  // chatter/"brrrt" instead of move). One frame per ~30ms slot, mimicking the real cadence.
  const uint8_t *preset_m_active_{nullptr};
  const uint8_t *preset_b_active_{nullptr};
  bool     preset_active_{false};
  uint32_t preset_t0_{0};
  int      preset_last_slot_{-1};
  static constexpr uint32_t PRESET_SLOT_MS = 30;   // handset frame cadence
  static constexpr int      PRESET_SLOTS   = 24;   // ~720ms: WAKE pair then RUN pair, then release
  // Slot map (30ms each), faithfully replaying the captured tap:
  //   WAKE pair → M@0, B@2  (engages the controller's direction relay — ONE clean click)
  //   ...idle (:A41;) gap...
  //   RUN  pair → M@18, B@20 (the motor "run" signal → brain auto-drives to the stored height)
  // Catch-up replay (service_preset_) guarantees no frame is skipped, so the wake is a single
  // clean engage rather than the relay chatter ("brrrt") a skipped/garbled sequence caused.
  static constexpr int PRESET_M1_SLOT = 0,  PRESET_B1_SLOT = 2;
  static constexpr int PRESET_M2_SLOT = 18, PRESET_B2_SLOT = 20;

  // ---- Diagnostics --------------------------------------------------------
  uint32_t rx_brain_{0}, rx_hand_{0}, tx_brain_{0}, tx_hand_{0};
  uint32_t frames_ok_{0}, frames_bad_{0};
  uint32_t last_poll_{0}, last_log_{0}, last_brain_frame_{0}, last_spoof_{0}, last_emit_{0};
  uint32_t last_version_{0}, last_ack2_{0};  // handset registration (version announce) + acks
  bool poll_enabled_{true};
  bool emulate_enabled_{true};   // ESP impersonates the handset (continuous heartbeat)
  static constexpr uint32_t SPOOF_INTERVAL_MS = 90;
  static constexpr uint32_t HANDSET_INTERVAL_MS = 30;   // idle B-heartbeat cadence
  static constexpr uint32_t MOVE_INTERVAL_MS = 12;      // fast M-frame cadence while a button is held
  static constexpr uint32_t POLL_INTERVAL_MS = 500;
  static constexpr uint32_t LOG_INTERVAL_MS  = 2000;

  // ---- Frame parsing -------------------------------------------------------
  struct FrameBuf {
    uint8_t data[32];
    int     len{0};
    bool    in_frame{false};
    bool feed(uint8_t c) {
      if (c == ':')      { in_frame = true; len = 0; data[len++] = c; }
      else if (in_frame) {
        if (len < (int)sizeof(data)-1) data[len++] = c;
        if (c == ';')    { in_frame = false; return true; }
      }
      return false;
    }
  } fb_brain_{}, fb_hand_{};

  // ---- Outbound frame queue (store-and-forward relay) ----------------------
  struct FrameQueue {
    uint8_t buf[6][32];
    uint8_t len[6];
    uint8_t head{0}, tail{0};
    bool push(const uint8_t *d, int n) {
      if (n > 32) n = 32;
      uint8_t nt = (uint8_t)((tail + 1) % 6);
      if (nt == head) return false;          // full → drop (keeps newest flowing)
      memcpy(buf[tail], d, n); len[tail] = (uint8_t)n; tail = nt; return true;
    }
    bool pop(uint8_t *d, int &n) {
      if (head == tail) return false;
      n = len[head]; memcpy(d, buf[head], n); head = (uint8_t)((head + 1) % 6); return true;
    }
    bool empty() const { return head == tail; }
  } to_hand_q_{}, to_brain_q_{};

  uint32_t last_rx_us_{0};                  // micros() of the most recent RX byte (either line)
  static constexpr uint32_t LINE_IDLE_US = 700;  // gap that means "between frames", safe to TX

  // ---- Command frames (decoded + verified against real DC-KS7 handset) -----
  static const uint8_t F_UP_M[8],  F_UP_B[8];
  static const uint8_t F_DN_M[8],  F_DN_B[8];
  static const uint8_t F_M1_M[8],  F_M1_B[8];
  static const uint8_t F_M2_M[8],  F_M2_B[8];
  static const uint8_t F_M3_M[8],  F_M3_B[8];
  static const uint8_t F_M4_M[8],  F_M4_B[8];
  static const uint8_t *const preset_m_[4];
  static const uint8_t *const preset_b_[4];
  // Handset version announce (captured from real DC-KS7 handset): :DC-KS7.V1.00.1817.0058E;
  static const uint8_t F_VERSION[25];

  // ---- Low-level UART I/O helpers -----------------------------------------
  void write_brain_(const uint8_t *buf, int len) { if (brain_uart_) { brain_uart_->write_array(buf, len); tx_brain_ += len; } }
  void write_hand_ (const uint8_t *buf, int len) { if (hand_uart_)  { hand_uart_->write_array(buf, len);  tx_hand_  += len; } }

  // ---- Checksum validation ------------------------------------------------
  static bool frame_checksum_ok(const uint8_t *d, int len) {
    if (len < 5 || d[0] != ':' || d[len-1] != ';') return false;
    uint16_t sum = 0;
    for (int i = 1; i < len - 3; i++) sum += d[i];
    sum &= 0xFF;
    char cc[4];
    snprintf(cc, sizeof(cc), "%02X", sum);
    return cc[0] == d[len-3] && cc[1] == d[len-2];
  }

  void log_frame_(const char *tag, const FrameBuf &f) {
    if (!verbose_) return;
    char hex[3*32+1] = {0};
    char asc[32+1]   = {0};
    int n = f.len > 32 ? 32 : f.len;
    for (int i = 0; i < n; i++) {
      snprintf(hex + i*3, 4, "%02X ", f.data[i]);
      uint8_t c = f.data[i];
      asc[i] = (c >= 0x20 && c < 0x7F) ? c : '.';
    }
    bool ok = frame_checksum_ok(f.data, f.len);
    ESP_LOGI("vivo_desk", "%s len=%d [%s] \"%s\" cksum=%s",
             tag, f.len, hex, asc, ok ? "OK" : "BAD");
  }

  // ---- Relay (receive side) -----------------------------------------------
  void relay_() {
    // brain → (queue to handset) + height parsing
    while (brain_uart_ && brain_uart_->available()) {
      uint8_t b = 0;
      if (!brain_uart_->read_byte(&b)) break;
      rx_brain_++; last_rx_us_ = micros();
      if (fb_brain_.feed(b)) {
        log_frame_("BRAIN", fb_brain_);
        bool ok = frame_checksum_ok(fb_brain_.data, fb_brain_.len);
        if (ok) frames_ok_++; else frames_bad_++;
        if (ok) {
          last_brain_frame_ = millis();
          parse_height_(fb_brain_);
          if (relay_enabled_) {
            write_hand_(fb_brain_.data, fb_brain_.len);  // forward to handset display (immediate; HW UART is full-duplex)
          } else if (ack_on_height_ && !emulate_enabled_ && !silent_listen_ && fb_brain_.data[1] == 'D') {
            send_ack_();  // legacy :A41; ACK (only when not emulating the handset)
          }
        }
      }
    }
    // handset → (queue to brain)
    while (hand_uart_ && hand_uart_->available()) {
      uint8_t b = 0;
      if (!hand_uart_->read_byte(&b)) break;
      rx_hand_++; last_rx_us_ = micros();
      if (fb_hand_.feed(b)) {
        log_frame_("HAND ", fb_hand_);
        // Forward the handset's frames to the brain — EXCEPT during a preset replay, when WE
        // own the brain stream entirely (relayed :A41; would collide with our K-frames and make
        // the brain's direction relay chatter instead of move). Held up/down still rides along.
        if (relay_enabled_ && !preset_active_ && frame_checksum_ok(fb_hand_.data, fb_hand_.len)) {
          write_brain_(fb_hand_.data, fb_hand_.len);
        }
      }
    }
  }

  // ---- HA command driver (relay mode) -------------------------------------
  // While HA is commanding (the relay handset->brain forward is suppressed, see relay_()),
  // act as the handset toward the brain: repeat the held up/down M-frame, or — for a preset
  // recall — repeat the M+B pair for the press window, then release so the brain drives itself
  // to the stored position while the normal handset heartbeat relay resumes.
  void drive_ha_() {
    uint32_t now = millis();
    if (move_dir_ != NONE) {                       // held up/down: repeat the M-frame
      if (now - last_emit_ < MOVE_INTERVAL_MS) return;
      last_emit_ = now;
      write_brain_(move_dir_ == UP ? F_UP_M : F_DN_M, 8);
      return;
    }
    service_preset_();
  }

  // Faithful preset replay: one frame per 30ms slot — M/B at the captured slots, :A41; idle on
  // the rest — for ~720ms, then release so the brain auto-drives. The relay is suppressed during
  // this (see relay_()), so the brain hears ONLY our clean handset-equivalent stream.
  void service_preset_() {
    if (!preset_active_) return;
    uint32_t now = millis();
    int cur = (int)((now - preset_t0_) / PRESET_SLOT_MS);
    // Advance ONE slot per loop: never SKIP a frame (a skipped wake = relay chatter / no move),
    // but never BUNCH frames back-to-back either (bunching malforms the sequence into a momentary
    // jog instead of the auto-drive recall). If the loop stalls past a slot we catch up one frame
    // per loop iteration, keeping them spaced — close enough to the handset's 30ms cadence.
    if (preset_last_slot_ < cur) {
      int slot = ++preset_last_slot_;
      if (slot >= PRESET_SLOTS) { preset_active_ = false; ESP_LOGI("vivo_desk", "preset replay done"); return; }
      if      (slot == PRESET_M1_SLOT || slot == PRESET_M2_SLOT) write_brain_(preset_m_active_, 8);
      else if (slot == PRESET_B1_SLOT || slot == PRESET_B2_SLOT) write_brain_(preset_b_active_, 8);
      else                                                       send_ack_();   // :A41; idle
    }
  }

  // ---- ACK / fallback poll (:A41;) ----------------------------------------
  void send_ack_() {
    static const uint8_t ACK[] = {0x3A,0x41,0x34,0x31,0x3B};  // :A41;
    write_brain_(ACK, sizeof(ACK));
  }

  // ---- Spoof brain → handset (capture handset's real ack) -----------------
  void spoof_height_() {
    if (!spoof_handset_) return;
    static const uint8_t H[] = {0x3A,0x44,0x20,0x37,0x31,0x2E,0x30,0x32,0x41,0x3B}; // :D 71.02A;
    uint32_t now = millis();
    if (now - last_spoof_ < SPOOF_INTERVAL_MS) return;
    last_spoof_ = now;
    write_hand_(H, sizeof(H));
  }

  // Fallback poll: only used if handset emulation is disabled (legacy :A41; mode).
  void poll_brain_() {
    if (!poll_enabled_ || spoof_handset_ || relay_enabled_ || emulate_enabled_) return;
    uint32_t now = millis();
    if (now - last_brain_frame_ < POLL_INTERVAL_MS) return;
    if (now - last_poll_ < POLL_INTERVAL_MS) return;
    last_poll_ = now;
    send_ack_();
  }

  // ---- Periodic stats log -------------------------------------------------
  void log_stats_() {
    uint32_t now = millis();
    if (now - last_log_ < LOG_INTERVAL_MS) return;
    last_log_ = now;
    ESP_LOGI("vivo_desk", "STATS rx_brain=%u rx_hand=%u tx_brain=%u tx_hand=%u frames_ok=%u frames_bad=%u relay=%s",
             rx_brain_, rx_hand_, tx_brain_, tx_hand_, frames_ok_, frames_bad_,
             relay_enabled_ ? "ON" : "OFF");
  }

  // ---- Height parsing -----------------------------------------------------
  void parse_height_(const FrameBuf &f) {
    if (f.len < 5 || f.data[1] != 'D' || !height_) return;
    if (!frame_checksum_ok(f.data, f.len)) return;
    int dlen = f.len - 5;
    if (dlen < 1) return;
    char buf[16] = {0};
    memcpy(buf, f.data + 2, dlen < 15 ? dlen : 15);
    float h = atof(buf);
    if (h > 0) height_->publish_state(h);
  }

  // ---- Handset emulation --------------------------------------------------
  // Empirical model (golden capture 2026-06-25): the real handset streams a continuous 'B'
  // heartbeat at idle (keeps the brain streaming :D<height>;), plus a periodic version announce
  // and :A41; acks, and a fast M-frame stream while a button is held.
  //   idle      → KUAB23 (F_UP_B) heartbeat every HANDSET_INTERVAL_MS
  //   up held   → KUAM2E (F_UP_M) only, every MOVE_INTERVAL_MS
  //   down held → KDAM1D (F_DN_M) only, every MOVE_INTERVAL_MS
  void emulate_handset_() {
    if (relay_enabled_ || spoof_handset_ || !emulate_enabled_) return;
    uint32_t now = millis();
    if (now - last_version_ >= 2000) { last_version_ = now; write_brain_(F_VERSION, sizeof(F_VERSION)); }
    if (now - last_ack2_    >= 500)  { last_ack2_ = now;    send_ack_(); }
    if (preset_active_) { service_preset_(); return; }   // preset recall in progress
    uint32_t interval = (move_dir_ == NONE) ? HANDSET_INTERVAL_MS : MOVE_INTERVAL_MS;
    if (now - last_emit_ < interval) return;
    last_emit_ = now;
    if      (move_dir_ == UP)   write_brain_(F_UP_M, 8);
    else if (move_dir_ == DOWN) write_brain_(F_DN_M, 8);
    else                        write_brain_(F_UP_B, 8);   // idle heartbeat
  }
};

// ---- Frame data definitions -----------------------------------------------
const uint8_t VivoDeskComponent::F_UP_M[8] = {0x3A,0x4B,0x55,0x41,0x4D,0x32,0x45,0x3B};
const uint8_t VivoDeskComponent::F_UP_B[8] = {0x3A,0x4B,0x55,0x41,0x42,0x32,0x33,0x3B};
const uint8_t VivoDeskComponent::F_DN_M[8] = {0x3A,0x4B,0x44,0x41,0x4D,0x31,0x44,0x3B};
const uint8_t VivoDeskComponent::F_DN_B[8] = {0x3A,0x4B,0x44,0x41,0x42,0x31,0x32,0x3B};
const uint8_t VivoDeskComponent::F_M1_M[8] = {0x3A,0x4B,0x20,0x31,0x4D,0x45,0x39,0x3B};
const uint8_t VivoDeskComponent::F_M1_B[8] = {0x3A,0x4B,0x20,0x31,0x42,0x44,0x45,0x3B};
const uint8_t VivoDeskComponent::F_M2_M[8] = {0x3A,0x4B,0x20,0x32,0x4D,0x45,0x41,0x3B};
const uint8_t VivoDeskComponent::F_M2_B[8] = {0x3A,0x4B,0x20,0x32,0x42,0x44,0x46,0x3B};
const uint8_t VivoDeskComponent::F_M3_M[8] = {0x3A,0x4B,0x20,0x33,0x4D,0x45,0x42,0x3B};
const uint8_t VivoDeskComponent::F_M3_B[8] = {0x3A,0x4B,0x20,0x33,0x42,0x45,0x30,0x3B};
const uint8_t VivoDeskComponent::F_M4_M[8] = {0x3A,0x4B,0x20,0x34,0x4D,0x45,0x43,0x3B};
const uint8_t VivoDeskComponent::F_M4_B[8] = {0x3A,0x4B,0x20,0x34,0x42,0x45,0x31,0x3B};

const uint8_t *const VivoDeskComponent::preset_m_[4] = {F_M1_M, F_M2_M, F_M3_M, F_M4_M};
const uint8_t *const VivoDeskComponent::preset_b_[4] = {F_M1_B, F_M2_B, F_M3_B, F_M4_B};

// :DC-KS7.V1.00.1817.0058E;
const uint8_t VivoDeskComponent::F_VERSION[25] = {
    0x3A,0x44,0x43,0x2D,0x4B,0x53,0x37,0x2E,0x56,0x31,0x2E,0x30,0x30,0x2E,
    0x31,0x38,0x31,0x37,0x2E,0x30,0x30,0x35,0x38,0x45,0x3B};

}  // namespace vivo_desk
}  // namespace esphome
