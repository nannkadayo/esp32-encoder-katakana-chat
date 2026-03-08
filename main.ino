/*
 * ESP-NOW カタカナ入力チャット装置 (u8g2版 / v3 + WebSerial対応)
 *
 * ─── WebSerial 追加仕様 ──────────────────────────────
 *  USBシリアル経由でWebページから操作可能
 *
 *  シリアルプロトコル（PC → ESP32）:
 *    'a'〜'z'  → SELECT_ALPHA モードに戻り alphaIndex を設定
 *    'X'+数字  → SELECT_EXT の extIndex を設定  例: "X2\n"
 *    'F'+数字  → SELECT_FINAL の finalIndex を設定  例: "F3\n"
 *    'E'       → Aボタン相当（確定 / 次ステップへ）
 *    'S'       → Bボタン相当（送信）
 *    'R'       → リセット（入力バッファクリア）
 *
 *  シリアルプロトコル（ESP32 → PC）:
 *    "STATE:mode|alpha|ext|final|buf|charCount\n"
 *    "MSG:>テキスト\n"   受信メッセージ
 *    "MSG:<テキスト\n"   送信メッセージ
 *    "OK\n"
 *    "INVALID\n"
 *    "READY\n"
 *
 *  ※ 区切り文字を | にすることで buf 内にカンマが入っても安全
 *
 * ★ peerAddress を相手の MAC に書き換えてください ★
 */

#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <U8g2lib.h>

// ===== ピン =====
#define SDA_PIN  21
#define SCL_PIN  22
#define ENC_CLK  16
#define ENC_DT   17
#define BTN_A     4
#define BTN_B    18

// ===== u8g2 =====
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
  U8G2_R0, U8X8_PIN_NONE, SCL_PIN, SDA_PIN
);

// ===== 相手のMAC =====
uint8_t peerAddress[] = {0x88,0x13,0xBF,0x69,0x20,0x90};

// =========================================================
// 変換テーブル
// =========================================================
const char* const alpha[26] = {
  "a","b","c","d","e","f","g","h","i","j",
  "k","l","m","n","o","p","q","r","s","t",
  "u","v","w","x","y","z"
};

#define EXT_A     0
#define EXT_I     1
#define EXT_U     2
#define EXT_E     3
#define EXT_O     4
#define EXT_YA    5
#define EXT_YU    6
#define EXT_YO    7
#define EXT_SMALL 8
#define EXT_N     9

const char* const extLabel[] = {
  "a","i","u","e","o","ya","yu","yo","small","n"
};
const int extCount[26] = {
  0, 8, 8, 5, 0, 5, 8, 8, 0, 8,
  8, 8, 8,10, 0, 8, 8, 8, 8, 9,
  0, 5, 5, 1, 5, 8
};
const char* const kanaTable[26][10] = {
/* a */ {"ア","イ","ウ","エ","オ",    nullptr,nullptr,nullptr,nullptr,nullptr},
/* b */ {"バ","ビ","ブ","ベ","ボ",    "ビャ","ビュ","ビョ",  nullptr,nullptr},
/* c */ {"カ","キ","ク","ケ","コ",    "キャ","キュ","キョ",  nullptr,nullptr},
/* d */ {"ダ","ヂ","ヅ","デ","ド",    nullptr,nullptr,nullptr,nullptr,nullptr},
/* e */ {"ア","イ","ウ","エ","オ",    nullptr,nullptr,nullptr,nullptr,nullptr},
/* f */ {"ファ","フィ","フ","フェ","フォ",nullptr,nullptr,nullptr,nullptr,nullptr},
/* g */ {"ガ","ギ","グ","ゲ","ゴ",    "ギャ","ギュ","ギョ",  nullptr,nullptr},
/* h */ {"ハ","ヒ","フ","ヘ","ホ",    "ヒャ","ヒュ","ヒョ",  nullptr,nullptr},
/* i */ {"ア","イ","ウ","エ","オ",    nullptr,nullptr,nullptr,nullptr,nullptr},
/* j */ {"ジャ","ジ","ジュ","ジェ","ジョ","ジャ","ジュ","ジョ",nullptr,nullptr},
/* k */ {"カ","キ","ク","ケ","コ",    "キャ","キュ","キョ",  nullptr,nullptr},
/* l */ {"ラ","リ","ル","レ","ロ",    "リャ","リュ","リョ",  nullptr,nullptr},
/* m */ {"マ","ミ","ム","メ","モ",    "ミャ","ミュ","ミョ",  nullptr,nullptr},
/* n */ {"ナ","ニ","ヌ","ネ","ノ",    "ニャ","ニュ","ニョ",  nullptr,"ン"},
/* o */ {"ア","イ","ウ","エ","オ",    nullptr,nullptr,nullptr,nullptr,nullptr},
/* p */ {"パ","ピ","プ","ペ","ポ",    "ピャ","ピュ","ピョ",  nullptr,nullptr},
/* q */ {"カ","キ","ク","ケ","コ",    "キャ","キュ","キョ",  nullptr,nullptr},
/* r */ {"ラ","リ","ル","レ","ロ",    "リャ","リュ","リョ",  nullptr,nullptr},
/* s */ {"サ","シ","ス","セ","ソ",    "シャ","シュ","ショ",  nullptr,nullptr},
/* t */ {"タ","チ","ツ","テ","ト",    "チャ","チュ","チョ",  "ッ",   nullptr},
/* u */ {"ア","イ","ウ","エ","オ",    nullptr,nullptr,nullptr,nullptr,nullptr},
/* v */ {"ヴァ","ヴィ","ヴ","ヴェ","ヴォ",nullptr,nullptr,nullptr,nullptr,nullptr},
/* w */ {"ワ","ウィ","ウ","ウェ","ヲ", nullptr,nullptr,nullptr,nullptr,nullptr},
/* x */ {nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,"→STEP3",nullptr},
/* y */ {"ヤ","イ","ユ","イェ","ヨ",  nullptr,nullptr,nullptr,nullptr,nullptr},
/* z */ {"ザ","ジ","ズ","ゼ","ゾ",    "ジャ","ジュ","ジョ",  nullptr,nullptr}
};
const char* const smallTable[5] = {"ァ","ィ","ゥ","ェ","ォ"};

// =========================================================
// パケット構造体
// =========================================================
#define MSG_BUF_SIZE 94

enum InputMode : uint8_t {
  SELECT_ALPHA = 0,
  SELECT_EXT   = 1,
  SELECT_FINAL = 2
};

typedef struct {
  char    text[MSG_BUF_SIZE];
  bool    typing;
  uint8_t inputMode;
  uint8_t alphaIndex;
  uint8_t extIndex;
  uint8_t finalIndex;
} ChatPacket;

ChatPacket txPkt, rxPkt;

// =========================================================
// 状態管理
// =========================================================
InputMode inputMode = SELECT_ALPHA;
int alphaIndex = 0;
int extIndex   = 0;
int finalIndex = 0;

#define MAX_MSG  2
#define MAX_CHAR 10
char chatBuffer[MAX_MSG][MSG_BUF_SIZE];
int  msgCount = 0;

bool    peerIsTyping  = false;
uint8_t peerInputMode = SELECT_ALPHA;
uint8_t peerAlphaIdx  = 0;
uint8_t peerExtIdx    = 0;
uint8_t peerFinalIdx  = 0;

char sendBuf[MSG_BUF_SIZE];
int  sendByteLen   = 0;
int  sendCharCount = 0;

int  lastEncoded  = 0;
int  encoderSteps = 0;

volatile bool needRedraw = true;

unsigned long lastBtnA = 0;
unsigned long lastBtnB = 0;
#define DEBOUNCE_MS 200

unsigned long lastPreviewSend = 0;
#define PREVIEW_INTERVAL_MS 50

bool hiddenMode = false;
unsigned long bPressHistory[3] = {0, 0, 0};
#define HIDDEN_WINDOW_MS 3000

// シリアル行バッファ
char serialLineBuf[32];
int  serialLineLen = 0;

// =========================================================
// ユーティリティ
// =========================================================
// ESP側の母音インデックス配列（a=0,i=8,u=20,e=4,o=14）
const int VOWEL_ALPHA_IDX[5] = {0, 8, 20, 4, 14};

bool isVowelAlpha(int idx) {
  for (int i = 0; i < 5; i++) if (VOWEL_ALPHA_IDX[i] == idx) return true;
  return false;
}
int alphaToVowelIndex(int idx) {
  for (int i = 0; i < 5; i++) if (VOWEL_ALPHA_IDX[i] == idx) return i;
  return 0;
}

const char* calcPreview(uint8_t mode, uint8_t ai, uint8_t ei, uint8_t fi) {
  if (mode == SELECT_ALPHA) return alpha[ai];
  if (mode == SELECT_EXT)   return (ai == 23) ? "sm" : kanaTable[ai][ei];
  return smallTable[fi];
}

// STATE を | 区切りでシリアル送信
void sendStateToSerial() {
  Serial.printf("STATE:%d|%d|%d|%d|%s|%d\n",
    (int)inputMode, alphaIndex, extIndex, finalIndex,
    sendBuf, sendCharCount);
}

void sendTypingPreview(bool force = false) {
  unsigned long now = millis();
  if (!force && (now - lastPreviewSend < PREVIEW_INTERVAL_MS)) return;
  lastPreviewSend = now;

  txPkt.text[0]    = '\0';
  txPkt.typing     = true;
  txPkt.inputMode  = (uint8_t)inputMode;
  txPkt.alphaIndex = (uint8_t)alphaIndex;
  txPkt.extIndex   = (uint8_t)extIndex;
  txPkt.finalIndex = (uint8_t)finalIndex;
  esp_now_send(peerAddress, (uint8_t*)&txPkt, sizeof(txPkt));
  sendStateToSerial();
}

void sendTypingStatus(bool isTyping) {
  txPkt.text[0]    = '\0';
  txPkt.typing     = isTyping;
  txPkt.inputMode  = (uint8_t)inputMode;
  txPkt.alphaIndex = (uint8_t)alphaIndex;
  txPkt.extIndex   = (uint8_t)extIndex;
  txPkt.finalIndex = (uint8_t)finalIndex;
  esp_now_send(peerAddress, (uint8_t*)&txPkt, sizeof(txPkt));
  sendStateToSerial();
}

void appendKana(const char* kana) {
  if (!kana || sendCharCount >= MAX_CHAR) return;
  int klen = strlen(kana);
  if (sendByteLen + klen < MSG_BUF_SIZE - 1) {
    memcpy(sendBuf + sendByteLen, kana, klen);
    sendByteLen += klen;
    sendBuf[sendByteLen] = '\0';
    sendCharCount++;
    sendTypingStatus(true);
  }
}

void resetInput() {
  inputMode  = SELECT_ALPHA;
  alphaIndex = 0;
  extIndex   = 0;
  finalIndex = 0;
}

void addToLog(char prefix, const char* msg) {
  char entry[MSG_BUF_SIZE + 1];
  entry[0] = prefix;
  strncpy(entry + 1, msg, MSG_BUF_SIZE - 2);
  entry[MSG_BUF_SIZE - 1] = '\0';
  if (msgCount < MAX_MSG) {
    strncpy(chatBuffer[msgCount++], entry, MSG_BUF_SIZE - 1);
  } else {
    strncpy(chatBuffer[0], chatBuffer[1], MSG_BUF_SIZE - 1);
    strncpy(chatBuffer[1], entry,         MSG_BUF_SIZE - 1);
  }
  chatBuffer[0][MSG_BUF_SIZE-1] = '\0';
  chatBuffer[1][MSG_BUF_SIZE-1] = '\0';
}

// =========================================================
// ★ WebSerial シリアル入力処理（行単位）
// =========================================================
void processSerialLine(const char* line) {
  int len = strlen(line);
  if (len == 0) return;

  // --- a〜z : alphaIndex 設定, SELECT_ALPHA へ ---
  if (len == 1 && line[0] >= 'a' && line[0] <= 'z') {
    alphaIndex = line[0] - 'a';
    inputMode  = SELECT_ALPHA;
    extIndex   = 0;
    finalIndex = 0;
    needRedraw = true;
    sendTypingPreview(true);
    Serial.println("OK");
    return;
  }

  // --- X0〜X9 : extIndex 設定（SELECT_EXT 中にのみ有効）---
  if (len == 2 && line[0] == 'X' && line[1] >= '0' && line[1] <= '9') {
    if (inputMode != SELECT_EXT) { Serial.println("INVALID"); return; }
    int idx = line[1] - '0';
    if (idx >= extCount[alphaIndex]) { Serial.println("INVALID"); return; }
    extIndex   = idx;
    needRedraw = true;
    sendTypingPreview(true);
    Serial.println("OK");
    return;
  }

  // --- F0〜F4 : finalIndex 設定（SELECT_FINAL 中にのみ有効）---
  if (len == 2 && line[0] == 'F' && line[1] >= '0' && line[1] <= '4') {
    if (inputMode != SELECT_FINAL) { Serial.println("INVALID"); return; }
    finalIndex = line[1] - '0';
    needRedraw = true;
    sendTypingPreview(true);
    Serial.println("OK");
    return;
  }

  // --- E : Aボタン相当 ---
  if (len == 1 && line[0] == 'E') {
    switch (inputMode) {
      case SELECT_ALPHA:
        if (isVowelAlpha(alphaIndex)) {
          appendKana(kanaTable[alphaIndex][alphaToVowelIndex(alphaIndex)]);
          resetInput();
        } else {
          if (extCount[alphaIndex] == 0) { Serial.println("INVALID"); return; }
          inputMode = SELECT_EXT;
          extIndex  = 0;
        }
        break;
      case SELECT_EXT:
        if (extIndex >= extCount[alphaIndex]) { Serial.println("INVALID"); return; }
        if (alphaIndex == 23) {              // 'x' → STEP3
          inputMode  = SELECT_FINAL;
          finalIndex = 0;
          break;
        }
        if (extIndex == EXT_SMALL && kanaTable[alphaIndex][EXT_SMALL] == nullptr) {
          Serial.println("INVALID"); return;
        }
        {
          const char* kana = kanaTable[alphaIndex][extIndex];
          if (!kana) { Serial.println("INVALID"); return; }
          appendKana(kana);
          resetInput();
        }
        break;
      case SELECT_FINAL:
        appendKana(smallTable[finalIndex]);
        resetInput();
        break;
    }
    sendTypingPreview(true);
    needRedraw = true;
    Serial.println("OK");
    return;
  }

  // --- S : Bボタン相当（送信）---
  if (len == 1 && line[0] == 'S') {
    if (sendCharCount == 0) { Serial.println("INVALID"); return; }
    sendMessage();
    // sendMessage 内で STATE 送信済み
    return;
  }

  // --- R : リセット ---
  if (len == 1 && line[0] == 'R') {
    sendBuf[0]  = '\0';
    sendByteLen = sendCharCount = 0;
    resetInput();
    needRedraw = true;
    sendTypingStatus(false);
    Serial.println("OK");
    return;
  }

  Serial.println("INVALID");
}

void handleSerialInput() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialLineLen > 0) {
        serialLineBuf[serialLineLen] = '\0';
        processSerialLine(serialLineBuf);
        serialLineLen = 0;
      }
    } else {
      if (serialLineLen < (int)sizeof(serialLineBuf) - 1) {
        serialLineBuf[serialLineLen++] = c;
      }
    }
  }
}

// =========================================================
// ESP-NOW
// =========================================================
void onReceive(const esp_now_recv_info *info, const uint8_t *data, int len) {
  if (len > (int)sizeof(rxPkt)) len = sizeof(rxPkt);
  memcpy(&rxPkt, data, len);
  rxPkt.text[MSG_BUF_SIZE - 1] = '\0';

  if (rxPkt.typing) {
    peerIsTyping  = true;
    peerInputMode = rxPkt.inputMode;
    peerAlphaIdx  = rxPkt.alphaIndex;
    peerExtIdx    = rxPkt.extIndex;
    peerFinalIdx  = rxPkt.finalIndex;
  } else {
    peerIsTyping = false;
    if (strlen(rxPkt.text) > 0) {
      addToLog('>', rxPkt.text);
      Serial.printf("MSG:>%s\n", rxPkt.text);
    }
  }
  needRedraw = true;
}

void sendMessage() {
  if (sendCharCount == 0) return;
  strncpy(txPkt.text, sendBuf, MSG_BUF_SIZE - 1);
  txPkt.text[MSG_BUF_SIZE - 1] = '\0';
  txPkt.typing     = false;
  txPkt.inputMode  = SELECT_ALPHA;
  txPkt.alphaIndex = 0;
  txPkt.extIndex   = 0;
  txPkt.finalIndex = 0;
  addToLog('<', txPkt.text);
  esp_now_send(peerAddress, (uint8_t*)&txPkt, sizeof(txPkt));
  Serial.printf("MSG:<%s\n", txPkt.text);

  sendBuf[0]  = '\0';
  sendByteLen = sendCharCount = 0;
  resetInput();
  sendTypingStatus(false);
  needRedraw = true;
  Serial.println("OK");
}

// =========================================================
// OLED 描画
// =========================================================
void drawOLED() {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 8, "LOG:");
  if (hiddenMode) u8g2.drawStr(70, 8, "[HIDDEN]");

  auto drawLogLine = [](int y, const char* line) {
    char pfx[2] = {line[0], '\0'};
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(0, y, pfx);
    u8g2.setFont(u8g2_font_b10_t_japanese1);
    u8g2.drawUTF8(8, y, line + 1);
  };

  if (msgCount == 1) {
    drawLogLine(20, chatBuffer[0]);
  } else if (msgCount >= 2) {
    drawLogLine(20, chatBuffer[msgCount - 2]);
    drawLogLine(32, chatBuffer[msgCount - 1]);
  }

  if (hiddenMode && peerIsTyping) {
    u8g2.setDrawColor(0);
    u8g2.drawBox(0, 22, 128, 12);
    u8g2.setDrawColor(1);
    const char* peerPrev = calcPreview(peerInputMode, peerAlphaIdx, peerExtIdx, peerFinalIdx);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(0, 32, "T>");
    char stepStr[6];
    snprintf(stepStr, sizeof(stepStr), "[S%d]", peerInputMode + 1);
    u8g2.drawStr(14, 32, stepStr);
    if (peerPrev) {
      bool isAscii = (peerPrev[0] < 0x80);
      u8g2.setFont(isAscii ? u8g2_font_5x7_tf : u8g2_font_b10_t_japanese1);
      if (isAscii) u8g2.drawStr(40, 32, peerPrev);
      else         u8g2.drawUTF8(40, 32, peerPrev);
    }
  }

  u8g2.drawHLine(0, 35, 128);

  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 44, "IN:");
  if (sendByteLen > 0) {
    u8g2.setFont(u8g2_font_b10_t_japanese1);
    u8g2.drawUTF8(18, 44, sendBuf);
  }

  u8g2.setFont(u8g2_font_5x7_tf);
  char posStr[6];
  snprintf(posStr, sizeof(posStr), "%d/10", sendCharCount);
  u8g2.drawStr(0, 63, posStr);

  u8g2.setFont(u8g2_font_b16_b_t_japanese1);
  char part1[10], part2[10], part3[10];

  if (inputMode == SELECT_ALPHA) {
    snprintf(part1, sizeof(part1), "[%s]", alpha[alphaIndex]);
    int w = u8g2.getStrWidth(part1);
    u8g2.drawStr(128 - w - 2, 63, part1);

  } else if (inputMode == SELECT_EXT) {
    snprintf(part1, sizeof(part1), "[%s]", alpha[alphaIndex]);
    snprintf(part2, sizeof(part2), "[%s]", extLabel[extIndex]);
    const char* preview = (alphaIndex == 23) ? "sm" : kanaTable[alphaIndex][extIndex];
    int w1 = u8g2.getStrWidth(part1);
    int w2 = u8g2.getStrWidth(part2);
    int wA = u8g2.getStrWidth(">");
    int wP = preview ? u8g2.getUTF8Width(preview) : 0;
    int x  = 128 - (w1 + w2 + wA + wP + 2) - 2;
    if (x < 28) x = 28;
    u8g2.drawStr(x, 63, part1); x += w1;
    u8g2.drawStr(x, 63, part2); x += w2;
    u8g2.drawStr(x, 63, ">");   x += wA;
    if (preview) u8g2.drawUTF8(x, 63, preview);

  } else {
    snprintf(part1, sizeof(part1), "[x]");
    snprintf(part2, sizeof(part2), "[sm]");
    snprintf(part3, sizeof(part3), "[%s]", extLabel[finalIndex]);
    const char* preview = smallTable[finalIndex];
    int w1 = u8g2.getStrWidth(part1);
    int w2 = u8g2.getStrWidth(part2);
    int w3 = u8g2.getStrWidth(part3);
    int wA = u8g2.getStrWidth(">");
    int wP = u8g2.getUTF8Width(preview);
    int x  = 128 - (w1 + w2 + w3 + wA + wP + 2) - 2;
    if (x < 28) x = 28;
    u8g2.drawStr(x, 63, part1); x += w1;
    u8g2.drawStr(x, 63, part2); x += w2;
    u8g2.drawStr(x, 63, part3); x += w3;
    u8g2.drawStr(x, 63, ">");   x += wA;
    u8g2.drawUTF8(x, 63, preview);
  }

  u8g2.sendBuffer();
}

// =========================================================
// エンコーダ
// =========================================================
void updateEncoder() {
  int msb = digitalRead(ENC_CLK);
  int lsb = digitalRead(ENC_DT);
  int encoded = (msb << 1) | lsb;
  int sum = (lastEncoded << 2) | encoded;

  if (sum==0b1101||sum==0b0100||sum==0b0010||sum==0b1011) encoderSteps++;
  if (sum==0b1110||sum==0b0111||sum==0b0001||sum==0b1000) encoderSteps--;
  lastEncoded = encoded;

  int delta = 0;
  if (encoderSteps >=  4) { delta =  1; encoderSteps = 0; }
  if (encoderSteps <= -4) { delta = -1; encoderSteps = 0; }

  if (delta != 0) {
    switch (inputMode) {
      case SELECT_ALPHA:
        alphaIndex = (alphaIndex + delta + 26) % 26;
        break;
      case SELECT_EXT: {
        int cnt = extCount[alphaIndex];
        extIndex = (extIndex + delta + cnt) % cnt;
        break;
      }
      case SELECT_FINAL:
        finalIndex = (finalIndex + delta + 5) % 5;
        break;
    }
    needRedraw = true;
    sendTypingPreview();
  }
}

// =========================================================
// 物理ボタン
// =========================================================
void handleButtons() {
  unsigned long now = millis();

  if (digitalRead(BTN_A) == LOW && now - lastBtnA > DEBOUNCE_MS) {
    lastBtnA = now;
    switch (inputMode) {
      case SELECT_ALPHA:
        if (isVowelAlpha(alphaIndex)) {
          appendKana(kanaTable[alphaIndex][alphaToVowelIndex(alphaIndex)]);
          resetInput();
        } else {
          inputMode = SELECT_EXT;
          extIndex  = 0;
        }
        break;
      case SELECT_EXT:
        if (alphaIndex == 23) { inputMode = SELECT_FINAL; finalIndex = 0; break; }
        if (extIndex == EXT_SMALL) { appendKana(kanaTable[alphaIndex][EXT_SMALL]); resetInput(); break; }
        { const char* kana = kanaTable[alphaIndex][extIndex]; if (kana) appendKana(kana); resetInput(); }
        break;
      case SELECT_FINAL:
        appendKana(smallTable[finalIndex]);
        resetInput();
        break;
    }
    sendTypingPreview(true);
    needRedraw = true;
  }

  if (digitalRead(BTN_B) == LOW && now - lastBtnB > DEBOUNCE_MS) {
    lastBtnB = now;
    bPressHistory[0] = bPressHistory[1];
    bPressHistory[1] = bPressHistory[2];
    bPressHistory[2] = now;

    bool isHiddenTrigger = (sendCharCount == 0)
                        && (inputMode == SELECT_ALPHA)
                        && (alphaIndex == 25);
    if ((bPressHistory[2] - bPressHistory[0] <= HIDDEN_WINDOW_MS) && isHiddenTrigger) {
      hiddenMode   = !hiddenMode;
      peerIsTyping = false;
      Serial.println(hiddenMode ? "[HIDDEN MODE ON]" : "[HIDDEN MODE OFF]");
      needRedraw = true;
      return;
    }
    sendMessage();
  }
}

// =========================================================
// setup / loop
// =========================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT,  INPUT_PULLUP);
  pinMode(BTN_A,   INPUT_PULLUP);
  pinMode(BTN_B,   INPUT_PULLUP);

  u8g2.begin();
  u8g2.enableUTF8Print();

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_b10_t_japanese1);
  u8g2.drawUTF8(4, 20, "カタカナチャット");
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(20, 36, "v3 + WebSerial");
  u8g2.drawStr(20, 50, "Initializing...");
  u8g2.sendBuffer();
  delay(800);

  lastEncoded = (digitalRead(ENC_CLK) << 1) | digitalRead(ENC_DT);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  Serial.print("My MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(0, 32, "ESP-NOW INIT FAIL");
    u8g2.sendBuffer();
    while (true) delay(1000);
  }
  esp_now_register_recv_cb(onReceive);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peerAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[WARN] Peer add failed");
  } else {
    Serial.println("[OK] Peer added");
  }

  needRedraw = true;
  Serial.println("READY");
}

void loop() {
  handleSerialInput();   // ★ WebSerial 入力を最初に処理
  updateEncoder();
  handleButtons();
  if (needRedraw) {
    drawOLED();
    needRedraw = false;
  }
  delay(1);
}
