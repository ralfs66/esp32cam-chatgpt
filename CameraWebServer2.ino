#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ===========================
// Select camera model in board_config.h
// ===========================
#include "board_config.h"

// Include the HTTP server implementation
#include "app_httpd.inc"

// ===========================
// WiFi Manager Configuration
// ===========================
WiFiManager wifiManager;

// ===========================
// OLED Display Configuration
// ===========================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ===========================
// Photo Upload Configuration
// ===========================
const char* uploadUrl = "https://www.yourphpserver.com/photo2.php";

// ===========================
// Boot Button Configuration
// ===========================
#define BOOT_BUTTON_PIN 0
bool lastButtonState = HIGH;
bool buttonPressed = false;
unsigned long buttonPressStartTime = 0;

// ===========================
// Function Declarations
// ===========================
void setupWiFiManager();
void uploadPhoto();
void enableFlashlight();
void disableFlashlight();
void setupOLED();
void displayText(const String& text);
void displayStatus(const String& status);
void checkBootButton();

// ===========================
// Global Variables
// ===========================
bool isUploading = false;
bool firstPhotoSent = false;

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;

  if (psramFound()) {
    Serial.println("PSRAM found, using high quality settings");
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count = 3;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    Serial.println("No PSRAM, using limited settings");
    config.frame_size = FRAMESIZE_QVGA;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.fb_count = 2;
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    if (s->id.PID == OV3660_PID) {
      s->set_vflip(s, 1);
      s->set_brightness(s, 1);
      s->set_saturation(s, -2);
    }
    s->set_framesize(s, FRAMESIZE_VGA);
    s->set_quality(s, 10);
    s->set_contrast(s, 1);
    s->set_brightness(s, 1);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

#if defined(LED_GPIO_NUM)
  setupLedFlash();
#endif

  setupWiFiManager();
  setupOLED();
  displayStatus("Kamera darbojas");

  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  startCameraServer();

  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");
  
  // Send first photo automatically after WiFi connection
  if (WiFi.status() == WL_CONNECTED && !firstPhotoSent) {
    Serial.println("Sending first photo automatically...");
    displayStatus("Domaju...");
    delay(2000); // Wait a moment for everything to settle
    uploadPhoto();
    firstPhotoSent = true;
  }
}

void loop() {
  checkBootButton();
  delay(100);
}

void setupWiFiManager() {
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.setConnectTimeout(30);
  
  String apName = "ESP32-Camera-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  
  Serial.println("Starting WiFi Manager...");
  Serial.print("AP Name: ");
  Serial.println(apName);
  
  if (!wifiManager.autoConnect(apName.c_str())) {
    Serial.println("Failed to connect and hit timeout");
    delay(3000);
    ESP.restart();
  }
  
  Serial.println("WiFi connected successfully!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void uploadPhoto() {
  if (isUploading) {
    Serial.println("Upload already in progress, skipping...");
    return;
  }
  
  isUploading = true;
  
  Serial.println("Taking photo...");
  displayStatus("Thinking...");
  
  enableFlashlight();
  delay(150);
  
  camera_fb_t *fb = esp_camera_fb_get();
  
  if (!fb) {
    Serial.println("Camera capture failed");
    displayStatus("Capture failed!");
    disableFlashlight();
    isUploading = false;
    return;
  }
  
  disableFlashlight();
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    displayStatus("No connection!");
    esp_camera_fb_return(fb);
    isUploading = false;
    return;
  }
  
  displayStatus("Analyzing...");
  
  HTTPClient http;
  http.begin(uploadUrl);
  http.addHeader("Content-Type", "image/jpeg");
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(30000);
  
  Serial.print("Uploading to: ");
  Serial.println(uploadUrl);
  Serial.printf("Photo size: %d bytes\n", fb->len);
  
  int httpResponseCode = http.POST(fb->buf, fb->len);
  esp_camera_fb_return(fb);
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    
    if (httpResponseCode == 200) {
      if (response.indexOf("<html>") != -1) {
        displayText("System error!");
      } else {
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, response);
        
        if (!error && doc.containsKey("text")) {
          String textToShow = doc["text"].as<String>();
          displayText(textToShow);
          Serial.print("Response: ");
          Serial.println(textToShow);
        } else {
          displayText("No data");
        }
      }
    } else {
      displayText("Upload failed!");
    }
  } else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
    displayText("Connection error!");
  }
  
  http.end();
  isUploading = false;
  Serial.println("Upload completed");
}

void enableFlashlight() {
#if defined(LED_GPIO_NUM)
  ledcWrite(LED_GPIO_NUM, 255);
  Serial.println("Flashlight enabled");
#endif
}

void disableFlashlight() {
#if defined(LED_GPIO_NUM)
  ledcWrite(LED_GPIO_NUM, 0);
  Serial.println("Flashlight disabled");
#endif
}

void setupOLED() {
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    return;
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  
  display.display();
  
  Serial.println("OLED Display initialized");
}

void displayText(const String& text) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  
  String words[20];
  int wordCount = 0;
  int startPos = 0;
  int spacePos = text.indexOf(' ');
  
  while (spacePos != -1 && wordCount < 19) {
    words[wordCount] = text.substring(startPos, spacePos);
    startPos = spacePos + 1;
    spacePos = text.indexOf(' ', startPos);
    wordCount++;
  }
  words[wordCount] = text.substring(startPos);
  wordCount++;
  
  String currentLine = "";
  for (int i = 0; i < wordCount; i++) {
    String testLine = currentLine + (currentLine.length() > 0 ? " " : "") + words[i];
    
    if (testLine.length() > 16) {
      if (currentLine.length() > 0) {
        display.println(currentLine);
        currentLine = words[i];
      } else {
        display.println(words[i].substring(0, 16));
        currentLine = "";
      }
    } else {
      currentLine = testLine;
    }
    
    if (display.getCursorY() > 60) {
      display.println("...");
      break;
    }
  }
  
  if (currentLine.length() > 0) {
    display.println(currentLine);
  }
  
  display.display();
}

void displayStatus(const String& status) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  
  display.println();
  display.println(status);
  display.display();
}

void checkBootButton() {
  bool currentButtonState = digitalRead(BOOT_BUTTON_PIN);
  
  if (lastButtonState == HIGH && currentButtonState == LOW) {
    buttonPressed = true;
    buttonPressStartTime = millis();
    Serial.println("Button pressed!");
    displayStatus("Ready...");
    delay(50);
  }
  
  if (lastButtonState == LOW && currentButtonState == HIGH && buttonPressed) {
    buttonPressed = false;
    unsigned long pressDuration = millis() - buttonPressStartTime;
    
    if (pressDuration > 100) {
      Serial.println("Taking photo!");
      if (WiFi.status() == WL_CONNECTED) {
        uploadPhoto();
      } else {
        displayStatus("No connection!");
        Serial.println("WiFi not connected");
      }
    }
  }
  
  lastButtonState = currentButtonState;
}
