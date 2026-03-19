#include <WiFi.h> 
#include <Arduino.h>
#include <HTTPClient.h> 
#include <Adafruit_GFX.h> 
#include <Adafruit_ST7789.h> 
#include <TJpg_Decoder.h> 
#include <ArduinoJson.h> 
#include "SFPRODISPLAYREGULAR15pt7b.h" 
#include "SFPRODISPLAYREGULAR10pt7b.h"
#include "SFPRODISPLAYREGULAR8pt7b.h"  

// ====== WiFi credentials ====== 
const char *ssid = ""; 
const char *password = ""; 
String serverUrl = ""; 

// ====== Server IP list ====== 
String servers[] = {
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  ""
};
int currentServerIndex = 0; 
unsigned long lastAttemptTime = 0; 
const unsigned long TIMEOUT_MS = 5000; // 5s timeout

// ====== ST7789 Pins ====== 
#define TFT_CS 10
#define TFT_DC 2
#define TFT_RST 1

#define TFT_BL 0          // Backlight pin
#define PWM_CHANNEL 0     // channel 0
#define PWM_FREQ 5000     // 5 kHz
#define PWM_RESOLUTION 8  // 8-bit

#define SCREEN_WIDTH 170 
#define SCREEN_HEIGHT 320 
#define BG_COLOR tft.color565(33, 33, 33) 

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST); 
uint16_t artistandTimeColor = tft.color565(200, 200, 200);
uint16_t barBGColor = tft.color565(16, 16, 16);  

// ====== Globals ======
String lastTitle = ""; 
String lastArtist = ""; 
String lastTime = ""; 
String lastDuration = ""; 
unsigned long songChangeTime = 0;  // for delaying duration

void NoticeServer() {
  tft.fillScreen(BG_COLOR); // optional, clear screen
  tft.setCursor(0, 0);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);

  tft.print("Check if Device is Connected to Same Network:\n");
  //tft.println(servers[currentServerIndex]);
}


bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) { 
  if (y >= SCREEN_HEIGHT) return false; 
  tft.startWrite(); 
  tft.setAddrWindow(x, y, w, h); 
  // Fix color order by swapping bytes 
  for (int i = 0; i < w * h; i++) { 
    uint16_t c = bitmap[i]; 
    bitmap[i] = (c >> 8) | (c << 8); 
  } 
  tft.writePixels(bitmap, w * h); 
  tft.endWrite(); 
  return true; 
} 

// Spinner globals
int spinnerFrame = 0;
int prevSpinnerFrame = -1;
unsigned long lastSpinnerUpdate = 0;
const unsigned long SPINNER_INTERVAL = 50; // ~60 FPS

// Song update timing
unsigned long lastSongUpdate = 0;
const unsigned long SONG_INTERVAL = 1000; // 1s refresh


// Spinner setup
void initSpinner() {
  int cx = tft.width() / 2;   
  int cy = tft.height() / 2;  
  int radius = 20;            
  int dotRadius = 3;          

  // Clear BG once
  tft.fillScreen(ST77XX_BLACK);

  // Draw all 8 orange dots once
  for (int i = 0; i < 8; i++) {
    float angle = i * 45 * DEG_TO_RAD;
    int x = cx + cos(angle) * radius;
    int y = cy + sin(angle) * radius;
    tft.fillCircle(x, y, dotRadius, ST77XX_ORANGE);
  }

  // Reset frame tracking
  spinnerFrame = 0;
  prevSpinnerFrame = -1;
}

// Frame update (just swap highlight)
void updateSpinnerFrame() {
  int cx = tft.width() / 2;
  int cy = tft.height() / 2;
  int radius = 20;
  int dotRadius = 3;

  // Erase previous highlight (back to orange)
  if (prevSpinnerFrame != -1) {
    float prevAngle = prevSpinnerFrame * 45 * DEG_TO_RAD;
    int px = cx + cos(prevAngle) * radius;
    int py = cy + sin(prevAngle) * radius;
    tft.fillCircle(px, py, dotRadius, ST77XX_ORANGE);
  }

  // Draw current highlight (white)
  float angle = spinnerFrame * 45 * DEG_TO_RAD;
  int x = cx + cos(angle) * radius;
  int y = cy + sin(angle) * radius;
  tft.fillCircle(x, y, dotRadius + 1, ST77XX_WHITE);

  prevSpinnerFrame = spinnerFrame;
  spinnerFrame = (spinnerFrame + 1) % 8;
}

void setup() { 
  Serial.begin(115200); 

  SPI.begin(6, -1, 7, 10);  // SCK=6, MISO=-1 (none), MOSI=7, SS=10

  // Attach pin directly with new API
  ledcAttachChannel(TFT_BL, PWM_FREQ, PWM_RESOLUTION, PWM_CHANNEL);
  // Set initial brightness (0–255)
  ledcWrite(TFT_BL, 200);


  tft.init(SCREEN_WIDTH, SCREEN_HEIGHT); 
  tft.setRotation(1); 
  tft.fillScreen(BG_COLOR); 
  tft.setTextColor(ST77XX_WHITE); 

  // Init JPEG decoder 
  TJpgDec.setJpgScale(1); 
  TJpgDec.setSwapBytes(true); 
  TJpgDec.setCallback(tft_output); 

  tft.setCursor(10, 10); 
  tft.print("Connecting WiFi..."); 

  WiFi.begin(ssid, password); 
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    Serial.print("."); 
  } 
  tft.fillScreen(BG_COLOR); 
  tft.setCursor(10, 10); 
  tft.print("WiFi Connected!"); 
} 

void drawAlbumArt(String url) { 
  HTTPClient http; 
  http.begin(url); 
  int httpCode = http.GET(); 
  if (httpCode == 200) { 
    int len = http.getSize(); 
    WiFiClient *stream = http.getStreamPtr(); 

    // Allocate buffer 
    uint8_t *buf = (uint8_t*)malloc(len); 
    if (buf) { 
      int index = 0; 
      while (http.connected() && (index < len)) { 
        int avail = stream->available(); 
        if (avail) { 
          int readBytes = stream->read(&buf[index], avail); 
          if (readBytes > 0) index += readBytes; 
        } 
      } 
      Serial.printf("Downloaded album art: %d bytes\n", index); 
      // Draw JPEG at top-left (0,0), resized on server side to 150x150 
      TJpgDec.drawJpg(5, 5, buf, len); 
      free(buf); 
    } else { 
      Serial.println("Buffer alloc failed!"); 
    } 
  } else { 
    Serial.printf("Album art HTTP error: %d\n", httpCode); 
  } 
  http.end(); 
} 

// --- Multi-line title ---
void printWrappedTextMultiLine(const GFXfont* font, int16_t x, int16_t y, int16_t maxWidth,
                               String text, uint16_t color, int16_t &endY, uint8_t maxLines) {
    tft.setFont(font);
    tft.setTextColor(color, BG_COLOR);

    // Clear block area
    tft.fillRect(x, y - font->yAdvance, maxWidth - x, font->yAdvance * maxLines, BG_COLOR);

    int16_t cursorY = y;
    int16_t lineHeight = font->yAdvance - 6;
    uint8_t lineCount = 0;
    String lineText = "";
    String word = "";

    for (int i = 0; i <= text.length(); i++) {
        char c = (i < text.length()) ? text[i] : ' '; // force flush at end
        if (c == ' ' || i == text.length()) {
            if (!(word == "" && c == ' ')) {

                // Check if word alone is too long for maxWidth
                int16_t bx, by;
                uint16_t bw, bh;
                tft.getTextBounds(word.c_str(), x, cursorY, &bx, &by, &bw, &bh);
                if (bw > (maxWidth - x)) {
                    String truncated = word;
                    while (truncated.length() > 0) {
                        tft.getTextBounds((truncated + "...").c_str(), x, cursorY, &bx, &by, &bw, &bh);
                        if (bw <= (maxWidth - x)) break;
                        truncated.remove(truncated.length() - 1);
                    }

                    tft.setCursor(x, cursorY);
                    tft.print(truncated + "...");
                    endY = cursorY + lineHeight;
                    return;
                }

                String testLine = (lineText.length() > 0 ? lineText + " " : "") + word;

                tft.getTextBounds(testLine.c_str(), x, cursorY, &bx, &by, &bw, &bh);

                if (bw <= (maxWidth - x)) {
                    lineText = testLine;
                } else {
                    // Print current line
                    tft.setCursor(x, cursorY);
                    if (lineCount + 1 >= maxLines) { // last line
                        String truncated = lineText;
                        while (truncated.length() > 0) {
                            tft.getTextBounds((truncated + "...").c_str(), x, cursorY, &bx, &by, &bw, &bh);
                            if (bw <= (maxWidth - x)) break;
                            truncated.remove(truncated.length() - 1);
                        }
                        tft.print(truncated + "...");
                        endY = cursorY + lineHeight;
                        return;
                    }

                    tft.print(lineText);
                    lineCount++;
                    cursorY += lineHeight;
                    lineText = word;
                }

                word = "";
            }
        } else {
            word += c;
        }
    }

    // Print remaining line
    if (lineText.length() > 0) {
        tft.setCursor(x, cursorY);
        tft.print(lineText);
        lineCount++;
    }
    endY = cursorY + lineHeight;
}

void printTruncatedSingleLine(const GFXfont* font, int16_t x, int16_t y, int16_t maxWidth,
                              String text, uint16_t color) {
    tft.setFont(font);
    tft.setTextColor(color, BG_COLOR);

    String displayText = text;
    int16_t bx, by;
    uint16_t bw, bh;
    tft.getTextBounds(displayText.c_str(), x, y, &bx, &by, &bw, &bh);

    while (bw > (maxWidth - x) && displayText.length() > 0) {
        displayText.remove(displayText.length() - 1);
        tft.getTextBounds((displayText + "...").c_str(), x, y, &bx, &by, &bw, &bh);
    }

    tft.setCursor(x, y);
    tft.print(displayText + (displayText.length() < text.length() ? "..." : ""));
}


// ====== Song Info ======
void drawSongInfo(String title, String artist, String duration) {
  tft.fillRect(156, 0, 164, 170, BG_COLOR);  

  int16_t nextY;
// Draw title 
printWrappedTextMultiLine(&SFPRODISPLAYREGULAR15pt7b, 165, 35, 310, title, ST77XX_WHITE, nextY, 3);

// Draw artist 
printTruncatedSingleLine(&SFPRODISPLAYREGULAR10pt7b, 165, nextY + 2, 310, artist, artistandTimeColor);



  songChangeTime = millis();  // delay duration drawing
  lastDuration = ""; 
  lastTitle = title; 
  lastArtist = artist; 
}

// ====== Duration Drawing ======
void drawDuration(String duration) {
  if (duration == "0:00") return;

  tft.setFont(&SFPRODISPLAYREGULAR8pt7b);
  tft.setTextColor(artistandTimeColor, BG_COLOR);

  String durStr = String(" : ") + duration;

  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(durStr.c_str(), 0, 0, &x1, &y1, &w, &h);

  int16_t posX = (310) - w;
  int16_t posY = 152;

  tft.fillRect(238, 140, 80, 20, BG_COLOR);
  tft.setCursor(posX, posY);
  tft.print(durStr);

  lastDuration = duration;
}

// ====== Time Updating ======
void updateTime(String currTime) {
  if (currTime == lastTime) return;

  tft.setFont(&SFPRODISPLAYREGULAR8pt7b);

  if (lastTime.length() > 0) {
    int16_t ox1, oy1;
    uint16_t ow, oh;
    tft.getTextBounds(lastTime.c_str(), 238, 152, &ox1, &oy1, &ow, &oh);
    tft.fillRect(238, 152 - oh, ow + 4, oh + 4, BG_COLOR);
  }

  tft.setTextColor(artistandTimeColor, BG_COLOR);
  tft.setCursor(238, 152);
  tft.print(currTime);

  lastTime = currTime;
}

// ====== Progress Bar ======
void updateProgressBar(String currTime, String duration) {
  auto timeToSeconds = [](String t) -> int {
    int colon = t.indexOf(':');
    if (colon == -1) return 0;
    int minutes = t.substring(0, colon).toInt();
    int seconds = t.substring(colon + 1).toInt();
    return minutes * 60 + seconds;
  };

  int currSec = timeToSeconds(currTime);
  int durSec  = timeToSeconds(duration);
  if (durSec <= 0) return;

  int barWidth = map(currSec, 0, durSec, 0, 315);
  tft.fillRect(5, 161, 315, 3, BG_COLOR);
  tft.fillRect(5, 161, 315, 3, barBGColor);
  tft.fillRect(5, 161, barWidth, 3, ST77XX_WHITE);
}

// Track the working server
int activeServerIndex = -1;  

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    // If we don't have a working server yet, scan all
    if (activeServerIndex == -1) {
      for (int i = 0; i < (sizeof(servers) / sizeof(servers[0])); i++) {
        http.begin(servers[i]);
        int httpCode = http.GET();
        if (httpCode == 200) {
          Serial.printf("Connected to server: %s\n", servers[i].c_str());
          activeServerIndex = i;
          break;
        }
        http.end();
      }
    }

    if (activeServerIndex != -1) {
      http.begin(servers[activeServerIndex]);
      int httpCode = http.GET();

      if (httpCode == 200) {
        String payload = http.getString();
        StaticJsonDocument<1024> doc;
        if (deserializeJson(doc, payload) == DeserializationError::Ok) {
          String title = doc["title"].as<String>();
          String artist = doc["artist"].as<String>();
          String currTime = doc["current_time"].as<String>();
          String duration = doc["duration"].as<String>();
          String state = doc["state"].as<String>();
          int playerState = state.toInt();

          if (playerState == 0) {
            if (prevSpinnerFrame == -1) {
              initSpinner();   // clear BG + draw base dots once
            }
            if (millis() - lastSpinnerUpdate >= SPINNER_INTERVAL) {
              updateSpinnerFrame(); // just update highlight
              lastSpinnerUpdate = millis();
              }
          } else {
            prevSpinnerFrame = -1;
            // Song updates every 1s
            if (millis() - lastSongUpdate >= SONG_INTERVAL) {
              if (title != lastTitle || artist != lastArtist) {
                tft.fillScreen(BG_COLOR);
                drawAlbumArt(doc["album_art"].as<String>());
                drawSongInfo(title, artist, duration);
              }
              updateTime(currTime);
              updateProgressBar(currTime, duration);
              if (millis() - songChangeTime > 1000) {
                if (duration != lastDuration && duration != "0:00") {
                  drawDuration(duration);
                }
              }
              lastSongUpdate = millis();
            }
          }
        }
      } else {
        Serial.printf("Lost connection to %s\n", servers[activeServerIndex].c_str());
        activeServerIndex = -1;
      }
      http.end();
    }
  }
}

