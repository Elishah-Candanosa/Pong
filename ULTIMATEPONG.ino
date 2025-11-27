/*
  Arduino Pong receiver (full-buffer variant) with OLED victory message (fixed)
  - Adds a short delay before deciding the winner so the final score frame has time to arrive.
  - Melody still starts immediately (unchanged).
  - Minimal changes only.
*/

#include <U8g2lib.h>
#include <avr/pgmspace.h>

// Use the constructor that worked in your test:
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0);

// Pins
const int POT_LEFT = A0;
const int POT_RIGHT = A1;
const int BTN1 = 2;
const int BTN2 = 3;
const int BUZZER = 6;

// Timing
const unsigned long SEND_INTERVAL_MS = 33UL;
unsigned long lastSendMillis = 0;

// RX buffer (short)
#define LINE_BUF_SIZE 64
char lineBuf[LINE_BUF_SIZE];
uint8_t linePos = 0;

// Display state
int lp_y = 32, rp_y = 32, ball_x = 64, ball_y = 32;
int score_l = 0, score_r = 0;

// Melody in PROGMEM to save RAM
const uint16_t melodyNotes[] PROGMEM = {784, 880, 1046, 784, 1046, 1175, 1568, 1760};
const uint16_t melodyDur[]   PROGMEM = {140, 120, 115, 140, 120, 120, 300, 320};
const uint8_t MELODY_LEN = sizeof(melodyNotes) / sizeof(melodyNotes[0]);

bool melodyPlaying = false;
uint8_t melodyIndex = 0;
unsigned long noteEndMillis = 0;
const unsigned long PAUSE_BETWEEN_NOTES = 40UL;

// Beep cooldown
unsigned long lastBeepMillis = 0;
const unsigned long BEEP_COOLDOWN = 40UL;

// Victory display state
bool victoryActive = false;
unsigned long victoryEndMillis = 0;
const unsigned long VICTORY_DISPLAY_MS = 2500UL; // how long to show message
// victorySide: 0=unknown, 1=left, 2=right
uint8_t victorySide = 0;

// New: triggered state to allow a short delay before showing overlay
bool victoryTriggered = false;
unsigned long victoryTriggeredMillis = 0;
const unsigned long VICTORY_DELAY_MS = 1000UL; // wait this long after 'W' to decide winner

void setup() {
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  Serial.begin(115200);
  u8g2.begin();

  lineBuf[0] = '\0';
  linePos = 0;

  // brief startup screen
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(8, 30, "PONG READY");
  u8g2.sendBuffer();
  delay(50);
}

static inline uint16_t pgm_read_u16_safe(const uint16_t *p) {
  return (uint16_t)pgm_read_word(p);
}

void startMelody() {
  melodyPlaying = true;
  melodyIndex = 0;
  uint16_t n = pgm_read_u16_safe(&melodyNotes[melodyIndex]);
  tone(BUZZER, n);
  uint16_t d = pgm_read_u16_safe(&melodyDur[melodyIndex]);
  noteEndMillis = millis() + d;
}

void updateMelody() {
  if (!melodyPlaying) return;
  unsigned long now = millis();
  if (now >= noteEndMillis) {
    noTone(BUZZER);
    melodyIndex++;
    if (melodyIndex >= MELODY_LEN) {
      melodyPlaying = false;
      return;
    }
    uint16_t n = pgm_read_u16_safe(&melodyNotes[melodyIndex]);
    uint16_t d = pgm_read_u16_safe(&melodyDur[melodyIndex]);
    noteEndMillis = now + PAUSE_BETWEEN_NOTES + d;
    tone(BUZZER, n);
  }
}

void playBlockingMelody() {
  for (uint8_t i = 0; i < MELODY_LEN; ++i) {
    uint16_t n = pgm_read_u16_safe(&melodyNotes[i]);
    uint16_t d = pgm_read_u16_safe(&melodyDur[i]);
    tone(BUZZER, n);
    delay(d);
    noTone(BUZZER);
    delay(30);
  }
}

void playBeep() {
  unsigned long now = millis();
  if (now - lastBeepMillis < BEEP_COOLDOWN) return;
  lastBeepMillis = now;
  tone(BUZZER, 900, 80);
}

int clampInt(int v, int a, int b) {
  if (v < a) return a;
  if (v > b) return b;
  return v;
}

void processLine(char *buf) {
  while (*buf == ' ' || *buf == '\t') buf++;
  if (*buf == '\0') return;

  // letter commands
  if ((buf[0] == 'B' || buf[0] == 'b') && buf[1] == '\0') { playBeep(); return; }

  if ((buf[0] == 'W' || buf[0] == 'w') && buf[1] == '\0') {
    // Host signals victory: start melody immediately,
    // but postpone deciding which side won so we can pick up the final score frame.
    startMelody();
    victoryTriggered = true;
    victoryTriggeredMillis = millis();
    // do NOT set victorySide here — wait until the delay so score_l/score_r can update
    return;
  }

  if ((buf[0] == 'V' || buf[0] == 'v') && buf[1] == '\0') { playBlockingMelody(); return; }

  // CSV parse up to 6 values
  const int MAXV = 6;
  int vals[MAXV] = {0,0,0,0,0,0};
  int idx = 0;
  char *token = strtok(buf, ",");
  while (token != NULL && idx < MAXV) {
    vals[idx++] = atoi(token);
    token = strtok(NULL, ",");
  }
  if (idx >= 4) {
    // Update displayed state (most recent scores)
    lp_y = clampInt(vals[0], 0, 63);
    rp_y = clampInt(vals[1], 0, 63);
    ball_x = clampInt(vals[2], 0, 127);
    ball_y = clampInt(vals[3], 0, 63);
    if (idx >= 6) { score_l = vals[4]; score_r = vals[5]; }
  }
}

void drawVictoryScreen() {
  u8g2.clearBuffer();

  // Draw a filled rectangle as background for the message (centered area)
  u8g2.drawBox(0, 18, 128, 28);

  // Use a small readable font so the message always fits.
  u8g2.setDrawColor(0); // draw text in "background" color so it appears light on dark box
  u8g2.setFont(u8g2_font_6x10_tr);

  // Two-line message: top line = side, bottom line = "gana!"
  const char *sideText;
  if (victorySide == 1) sideText = "Izquierda";
  else if (victorySide == 2) sideText = "Derecha";
  else sideText = "GANA";

  const char *line2 = "gana!";

  int w1 = u8g2.getStrWidth(sideText);
  int w2 = u8g2.getStrWidth(line2);

  u8g2.drawStr((128 - w1) / 2, 28, sideText); // y ~28 fits inside box
  u8g2.drawStr((128 - w2) / 2, 40, line2);    // second line

  // Restore draw color to normal and show scores in the area below the box
  u8g2.setDrawColor(1);
  char s[20];
  snprintf(s, sizeof(s), "%d  -  %d", score_l, score_r);
  int sw = u8g2.getStrWidth(s);
  u8g2.drawStr((128 - sw) / 2, 56, s); // small score line near bottom

  u8g2.sendBuffer();
}

void loop() {
  unsigned long now = millis();

  // periodic host readings (optional)
  if (now - lastSendMillis >= SEND_INTERVAL_MS) {
    int pL = analogRead(POT_LEFT);
    int pR = analogRead(POT_RIGHT);
    int b1 = digitalRead(BTN1);
    int b2 = digitalRead(BTN2);
    Serial.print(pL); Serial.print(",");
    Serial.print(pR); Serial.print(",");
    Serial.print(b1); Serial.print(",");
    Serial.println(b2);
    lastSendMillis = now;
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }

  // read serial non-blocking
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      lineBuf[linePos] = '\0';
      if (linePos > 0) processLine(lineBuf);
      linePos = 0;
      lineBuf[0] = '\0';
    } else {
      if (linePos < LINE_BUF_SIZE - 1) lineBuf[linePos++] = c;
      else { linePos = 0; lineBuf[0] = '\0'; }
    }
  }

  // update melody
  updateMelody();

  // If a victory was triggered recently, and the delay has elapsed, decide winner and activate overlay
  if (victoryTriggered && (millis() - victoryTriggeredMillis >= VICTORY_DELAY_MS)) {
    // decide winner using the most recent score_l/score_r
    if (score_l > score_r) victorySide = 1;
    else if (score_r > score_l) victorySide = 2;
    else victorySide = 0;
    victoryActive = true;
    victoryEndMillis = millis() + VICTORY_DISPLAY_MS;
    victoryTriggered = false;
  }

  // If victory overlay active, draw it (and skip regular frame drawing).
  if (victoryActive) {
    drawVictoryScreen();
    if (millis() >= victoryEndMillis) {
      victoryActive = false;
      victorySide = 0;
    }
    // continue loop so we still read serial and update melody while showing message
    return;
  }

  // draw full frame (game) - ORIGINAL ELLIPSE SIZES RESTORED
  u8g2.clearBuffer();

  // dibujado en Arduino: paletas y bola with sizes you used previously
  u8g2.drawEllipse(8, clampInt(lp_y,0,63), 3, 6, U8G2_DRAW_ALL);
  u8g2.drawEllipse(120, clampInt(rp_y,0,63), 3, 6, U8G2_DRAW_ALL);
  u8g2.drawEllipse(clampInt(ball_x,0,127), clampInt(ball_y,0,63), 3, 3, U8G2_DRAW_ALL);

  // center dotted line
  for (int y = 0; y < 64; y += 8) u8g2.drawVLine(64, y, 4);

  u8g2.setFont(u8g2_font_6x10_tr);
  char b[16];
  snprintf(b, sizeof(b), "%d", score_l);
  u8g2.drawStr(4, 12, b);
  snprintf(b, sizeof(b), "%d", score_r);
  u8g2.drawStr(118 - u8g2.getStrWidth(b), 12, b);

  u8g2.sendBuffer();

  delay(1);
}
