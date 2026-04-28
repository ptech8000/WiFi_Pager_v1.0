#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include "esp_sleep.h"

// ==================== CONFIGURATION ====================
const char* home_ssid = "P-TECH";     
const char* home_password = "wirewizard";
const char* ap_ssid = "Smart_Comm";
const char* ap_password = "12345678";

// ==================== FIXED PIN CONFIG (ESP32-C3 SAFE) ====================
#define BTN_UP       2   
#define BTN_DOWN     3   
#define BTN_SELECT   4   
#define BTN_BACK     5   
#define BTN_DELETE   8
#define BTN_POWER    9   

// Display Configuration
#define TFT_CS      10
#define TFT_RST     1
#define TFT_DC      0
#define TFT_BL      21
#define TFT_MOSI    7
#define TFT_SCLK    6

// Buzzer Configuration
#define BUZZER_PIN  20

// ==================== POWER STATES ====================
enum PowerState {
    POWER_ACTIVE,
    POWER_IDLE,
    POWER_SLEEP
};

PowerState powerState = POWER_ACTIVE;
unsigned long lastActivityTime = 0;
const unsigned long screenTimeout = 30000;
const unsigned long idleTimeout = 15000;
const unsigned long sleepTimeout = 300000;
bool uiIdle = false;

// ==================== UI OPTIMIZATION FLAGS ====================
bool uiNeedsUpdate = true;
bool contentDirty = true;
bool headerDirty = true;
bool footerDirty = true;
unsigned long lastUIUpdate = 0;
const int UI_REFRESH_INTERVAL = 200;

// ==================== PRESET REPLIES ====================
const char* presetReplies[] = {
    "OK, got it", "Okay Sir","On my way", "Busy, call later", "Thank you",
    "I understand", "Please send more info", "Call me", "See you soon",
    "Running late", "Can you repeat?", "Yes", "No", "Maybe later",
    "Working on it", "Done", "Need help", "I dey jare", "Wetin sup", "What!!!"
    "As how nah", "Any gist?", "talk To me", "I'm Fine", "Fine", "Fine Girl", "Fine boy", "Opoor"
    "imagine", "Nothing", "Hmmmm!!!", "okay","where are you?", "Fuck you", "I need money",
    "WTF!!!", "OMG!!!", "let's meet", " Come here", "let's talk", "Should I come?", "Nice one",
    "What's that??", "Anything for me??", "Sorry"
};
const int replyCount = 42;
int selectedReplyIndex = 0;
bool inReplyMenu = false;

// ==================== BUZZER SETTINGS ====================
int currentSound = 0;
bool buzzerEnabled = true;

// ==================== DISPLAY UI COLORS ====================
#define COLOR_BG      0x0000
#define COLOR_TEXT    0xFFFF
#define COLOR_ACCENT  0x07E0
#define COLOR_WARNING 0xF800
#define COLOR_INFO    0x051F
#define COLOR_HEADER  0xFFE0
#define COLOR_SELECT  0x07FF
#define COLOR_MENU    0xFD20

// ==================== GLOBAL VARIABLES ====================
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
WebServer server(80);

struct MessageItem {
    String text;
    String timestamp;
    bool read;
    bool priority;
};

MessageItem messageQueue[50];
int messageCount = 0;
int currentMessageIndex = 0;
int scrollPosition = 0;
int visibleLines = 7;
bool screenOn = true;
bool inMenu = false;
int menuOption = 0;
int unreadCount = 0;

String cachedWrappedText = "";
int cachedMessageIndex = -1;

unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 50;
unsigned long lastButtonCheck = 0;

bool lastUpState = HIGH;
bool lastDownState = HIGH;
bool lastSelectState = HIGH;
bool lastBackState = HIGH;
bool lastDeleteState = HIGH;
bool lastPowerState = HIGH;

unsigned long notificationEndTime = 0;
bool showingNotification = false;

enum NetworkMode { MODE_AP, MODE_STA };
NetworkMode currentMode = MODE_AP;
IPAddress localIP;

const char* menuOptions[] = {"Send Reply", "Delete Message", "Clear All", "Sound Settings", "Reconnect WiFi"};
const int menuCount = 5;

bool inSoundMenu = false;
int soundMenuOption = 0;

enum ScreenState { SCREEN_HOME, SCREEN_MESSAGES, SCREEN_MENU, SCREEN_REPLY, SCREEN_SOUND };
ScreenState currentScreen = SCREEN_HOME;

int prevSelectedReplyIndex = -1;
int prevMenuOption = -1;
int prevSoundMenuOption = -1;
int prevScrollPosition = -1;
int prevMessageIndex = -1;
bool footerDrawn = false;

unsigned long lastWiFiReconnectAttempt = 0;
const unsigned long wifiReconnectInterval = 30000;
bool wifiConnected = false;

// ==================== FUNCTION DECLARATIONS ====================
void quickBeep();
void doubleBeep();
void longBeep();
void playPriorityAlert();
void playNormalBeep();
void playReplyTone();
String getTimestamp();
void showNotification(String message, uint16_t color, int duration);
void clearNotification();
String wrapText(String text, int maxChars);
void drawSoundMenu();
void handleSoundMenu();
void drawReplyMenu();
void sendSelectedReply();
void drawMainMenu();
void executeMenuOption();
void quickDeleteCurrentMessage();
void updateUnreadCount();
void drawHeader(String title, uint16_t color);
void drawFooter();
void drawMessageContent();
void drawHomeScreen();
void checkButtons();
void addNewMessage(String text);
void updateDisplay(String text);
bool connectToWiFi();
void startAccessPoint();
void handleRoot();
void handleSend();
void handleClear();
void handleNotFound();
void handleStatus();
void handleMessages();
void clearContentArea();
void updateUI();
void updateBacklight();
void goToSleep();
void checkWiFiConnection();
void reconnectWiFi();

// ==================== SMART NOTIFICATION SOUNDS ====================
void quickBeep() {
    if (!buzzerEnabled) return;
    digitalWrite(BUZZER_PIN, HIGH);
    delay(50);
    digitalWrite(BUZZER_PIN, LOW);
}

void doubleBeep() {
    if (!buzzerEnabled) return;
    quickBeep();
    delay(100);
    quickBeep();
}

void longBeep() {
    if (!buzzerEnabled) return;
    digitalWrite(BUZZER_PIN, HIGH);
    delay(300);
    digitalWrite(BUZZER_PIN, LOW);
}

void playPriorityAlert() {
    if (!buzzerEnabled) return;
    for (int i = 0; i < 3; i++) {
        longBeep();
        delay(150);
    }
}

void playNormalBeep() {
    if (!buzzerEnabled) return;
    quickBeep();
}

void playReplyTone() {
    if (!buzzerEnabled) return;
    doubleBeep();
}

// ==================== BACKLIGHT CONTROL ====================
void updateBacklight() {
    if (!screenOn) {
        digitalWrite(TFT_BL, LOW);
        return;
    }
    digitalWrite(TFT_BL, HIGH);
}

// ==================== WIFI MANAGEMENT ====================
void checkWiFiConnection() {
    if (currentMode == MODE_STA) {
        if (WiFi.status() != WL_CONNECTED) {
            unsigned long now = millis();
            if (now - lastWiFiReconnectAttempt > wifiReconnectInterval) {
                lastWiFiReconnectAttempt = now;
                reconnectWiFi();
            }
        } else {
            wifiConnected = true;
        }
    }
}

void reconnectWiFi() {
    Serial.println("Attempting to reconnect to WiFi...");
    showNotification("Reconnecting to WiFi...", COLOR_INFO, 2000);
    
    WiFi.disconnect();
    delay(1000);
    WiFi.begin(home_ssid, home_password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(200);
        attempts++;
        yield();
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        localIP = WiFi.localIP();
        wifiConnected = true;
        showNotification("WiFi Reconnected!", COLOR_ACCENT, 2000);
        Serial.print("Reconnected! IP: ");
        Serial.println(localIP);
        
        if (currentScreen == SCREEN_HOME) {
            drawHomeScreen();
        }
    } else {
        wifiConnected = false;
        showNotification("WiFi Failed!", COLOR_WARNING, 2000);
        Serial.println("WiFi reconnection failed");
    }
}

bool connectToWiFi() {
    Serial.print("Connecting to WiFi SSID: ");
    Serial.println(home_ssid);
    
    tft.fillScreen(COLOR_BG);
    tft.setTextColor(COLOR_INFO);
    tft.setTextSize(2);
    tft.setCursor(20, 80);
    tft.print("Connecting to");
    tft.setCursor(20, 110);
    tft.print(home_ssid);
    tft.setTextSize(1);
    tft.setCursor(20, 150);
    tft.print("Please wait...");
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(home_ssid, home_password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(200);
        attempts++;
        Serial.print(".");
        yield();
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
        localIP = WiFi.localIP();
        currentMode = MODE_STA;
        wifiConnected = true;
        WiFi.setSleep(true);
        
        Serial.println("WiFi Connected!");
        Serial.print("IP Address: ");
        Serial.println(localIP);
        
        return true;
    } else {
        Serial.println("WiFi Connection Failed!");
        wifiConnected = false;
        return false;
    }
}

void startAccessPoint() {
    Serial.println("Starting Access Point...");
    
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid, ap_password);
    localIP = WiFi.softAPIP();
    currentMode = MODE_AP;
    wifiConnected = false;
    WiFi.setSleep(true);
    
    Serial.print("AP Mode Started! IP: ");
    Serial.println(localIP);
}

// ==================== POWER MANAGEMENT ====================
void goToSleep() {
    Serial.println("Entering sleep mode...");
    
    tft.fillScreen(COLOR_BG);
    tft.setTextColor(COLOR_INFO);
    tft.setTextSize(2);
    tft.setCursor(30, 100);
    tft.print("Sleep Mode");
    tft.setTextSize(1);
    tft.setCursor(50, 130);
    tft.print("Press any key");
    delay(2000);
    
    digitalWrite(TFT_BL, LOW);
    
    esp_sleep_enable_gpio_wakeup();
    gpio_wakeup_enable((gpio_num_t)BTN_UP, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)BTN_DOWN, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)BTN_SELECT, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)BTN_BACK, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)BTN_DELETE, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)BTN_POWER, GPIO_INTR_LOW_LEVEL);
    
    esp_light_sleep_start();
    
    Serial.println("Waking up...");
    
    SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
    tft.init(240, 240);
    tft.setRotation(1);
    tft.setTextWrap(false);
    
    digitalWrite(TFT_BL, HIGH);
    screenOn = true;
    currentScreen = SCREEN_MESSAGES;
    contentDirty = true;
    headerDirty = true;
    uiNeedsUpdate = true;
    lastActivityTime = millis();
    powerState = POWER_ACTIVE;
    uiIdle = false;
    
    showNotification("Waking up...", COLOR_ACCENT, 1000);
}

// ==================== UTILITY FUNCTIONS ====================
String getTimestamp() {
    unsigned long seconds = millis() / 1000;
    int hours = (seconds % 86400) / 3600;
    int minutes = (seconds % 3600) / 60;
    int secs = seconds % 60;
    char timeString[20];
    sprintf(timeString, "%02d:%02d:%02d", hours, minutes, secs);
    return String(timeString);
}

void showNotification(String message, uint16_t color, int duration = 1500) {
    showingNotification = true;
    notificationEndTime = millis() + duration;
    
    tft.fillRect(0, 0, 240, 25, color);
    tft.setTextColor(COLOR_BG);
    tft.setTextSize(1);
    tft.setCursor(10, 8);
    if (message.length() > 30) {
        tft.print(message.substring(0, 28) + "..");
    } else {
        tft.print(message);
    }
}

void clearNotification() {
    if (showingNotification && millis() > notificationEndTime) {
        showingNotification = false;
        if (inReplyMenu) {
            currentScreen = SCREEN_REPLY;
            drawReplyMenu();
        }
        else if (inSoundMenu) {
            currentScreen = SCREEN_SOUND;
            drawSoundMenu();
        }
        else if (inMenu) {
            currentScreen = SCREEN_MENU;
            drawMainMenu();
        }
        else {
            currentScreen = SCREEN_MESSAGES;
            contentDirty = true;
            uiNeedsUpdate = true;
        }
    }
}

// ==================== TEXT WRAPPING ====================
String wrapText(String text, int maxChars) {
    if (currentMessageIndex == cachedMessageIndex && cachedWrappedText.length() > 0) {
        return cachedWrappedText;
    }
    
    String result = "";
    String line = "";
    String word = "";
    
    for (int i = 0; i < text.length(); i++) {
        char c = text.charAt(i);
        
        if (c == '\n') {
            if (word.length() > 0) {
                if (line.length() == 0) {
                    line = word;
                } else if (line.length() + word.length() + 1 > maxChars) {
                    result += line + "\n";
                    line = word;
                } else {
                    line += " " + word;
                }
                word = "";
            }
            if (line.length() > 0) {
                result += line + "\n";
                line = "";
            }
            result += "\n";
        } else if (c == ' ') {
            if (word.length() > 0) {
                if (line.length() == 0) {
                    line = word;
                } else if (line.length() + word.length() + 1 > maxChars) {
                    result += line + "\n";
                    line = word;
                } else {
                    line += " " + word;
                }
                word = "";
            }
        } else {
            word += c;
        }
    }
    
    if (word.length() > 0) {
        if (line.length() == 0) {
            line = word;
        } else if (line.length() + word.length() + 1 > maxChars) {
            result += line + "\n";
            line = word;
        } else {
            line += " " + word;
        }
    }
    
    if (line.length() > 0) {
        result += line;
    }
    
    cachedWrappedText = result;
    cachedMessageIndex = currentMessageIndex;
    
    return result;
}

void clearContentArea() {
    tft.fillRect(0, 30, 240, 190, COLOR_BG);
}

// ==================== UI UPDATE SYSTEM ====================
void updateUI() {
    if (headerDirty) {
        drawHeader("MESSAGES", COLOR_HEADER);
        headerDirty = false;
    }
    
    if (contentDirty) {
        drawMessageContent();
        contentDirty = false;
    }
    
    if (footerDirty && !footerDrawn) {
        drawFooter();
        footerDrawn = true;
        footerDirty = false;
    }
}

// ==================== SOUND SETTINGS MENU ====================
void drawSoundMenu() {
    bool screenChanged = (currentScreen != SCREEN_SOUND);
    currentScreen = SCREEN_SOUND;
    
    if (screenChanged) {
        tft.fillScreen(COLOR_BG);
        tft.fillRect(0, 0, 240, 30, COLOR_SELECT);
        tft.setTextColor(COLOR_BG);
        tft.setTextSize(2);
        tft.setCursor(10, 5);
        tft.print("Sound Settings");
        tft.setTextSize(1);
        tft.setCursor(180, 10);
        tft.print("BACK");
        
        tft.fillRect(0, 220, 240, 20, COLOR_SELECT);
        tft.setTextColor(COLOR_BG);
        tft.setCursor(10, 226);
        tft.print("UP/DOWN=Navigate  SEL=Select  BACK=Exit");
    } else {
        clearContentArea();
    }
    
    const char* soundOptions[] = {"Normal Beep", "Double Beep", "Enable/Disable"};
    for (int i = 0; i < 3; i++) {
        int y = 60 + (i * 40);
        if (i == soundMenuOption) {
            tft.fillRoundRect(10, y - 5, 220, 30, 5, COLOR_SELECT);
            tft.setTextColor(COLOR_BG);
        } else {
            tft.setTextColor(COLOR_TEXT);
        }
        tft.setTextSize(2);
        tft.setCursor(20, y + 5);
        tft.print(soundOptions[i]);
        if (i == 0 && currentSound == 0) tft.print(" *");
        if (i == 1 && currentSound == 1) tft.print(" *");
        if (i == 2) {
            tft.setCursor(180, y + 5);
            tft.print(buzzerEnabled ? "ON" : "OFF");
        }
    }
    
    prevSoundMenuOption = soundMenuOption;
}

void handleSoundMenu() {
    switch(soundMenuOption) {
        case 0: 
            currentSound = 0; 
            quickBeep(); 
            drawSoundMenu(); 
            break;
        case 1: 
            currentSound = 1; 
            doubleBeep(); 
            drawSoundMenu(); 
            break;
        case 2: 
            buzzerEnabled = !buzzerEnabled; 
            drawSoundMenu(); 
            break;
    }
}

// ==================== PRESET REPLY MENU ====================
void drawReplyMenu() {
    bool screenChanged = (currentScreen != SCREEN_REPLY);
    currentScreen = SCREEN_REPLY;
    
    if (screenChanged) {
        tft.fillScreen(COLOR_BG);
        tft.fillRect(0, 0, 240, 30, COLOR_SELECT);
        tft.setTextColor(COLOR_BG);
        tft.setTextSize(2);
        tft.setCursor(10, 5);
        tft.print("Select Reply");
        tft.setTextSize(1);
        tft.setCursor(180, 10);
        tft.print("BACK");
        
        tft.fillRect(0, 220, 240, 20, COLOR_SELECT);
        tft.setTextColor(COLOR_BG);
        tft.setCursor(10, 226);
        tft.print("UP/DOWN=Navigate  SEL=Send  BACK=Cancel");
    } else if (selectedReplyIndex != prevSelectedReplyIndex) {
        clearContentArea();
    }
    
    int startReply = max(0, selectedReplyIndex - 3);
    int endReply = min(replyCount, startReply + 7);
    
    for (int i = startReply; i < endReply; i++) {
        int y = 50 + ((i - startReply) * 24);
        if (i == selectedReplyIndex) {
            tft.fillRoundRect(5, y - 2, 230, 22, 3, COLOR_SELECT);
            tft.setTextColor(COLOR_BG);
        } else {
            tft.setTextColor(COLOR_TEXT);
        }
        tft.setTextSize(1);
        tft.setCursor(10, y + 4);
        tft.print(i + 1);
        tft.print(". ");
        tft.print(presetReplies[i]);
    }
    
    prevSelectedReplyIndex = selectedReplyIndex;
}

void sendSelectedReply() {
    String reply = presetReplies[selectedReplyIndex];
    addNewMessage("[REPLY] " + reply);
    playReplyTone();
    inReplyMenu = false;
    currentScreen = SCREEN_MESSAGES;
    contentDirty = true;
    headerDirty = true;
    uiNeedsUpdate = true;
}

// ==================== MAIN MENU ====================
void drawMainMenu() {
    bool screenChanged = (currentScreen != SCREEN_MENU);
    currentScreen = SCREEN_MENU;
    
    if (screenChanged) {
        tft.fillScreen(COLOR_BG);
        tft.fillRect(0, 0, 240, 30, COLOR_MENU);
        tft.setTextColor(COLOR_BG);
        tft.setTextSize(2);
        tft.setCursor(10, 5);
        tft.print("MENU");
        tft.setTextSize(1);
        tft.setCursor(180, 10);
        tft.print("EXIT");
        
        tft.fillRect(0, 220, 240, 20, COLOR_MENU);
        tft.setTextColor(COLOR_BG);
        tft.setCursor(10, 226);
        tft.print("UP/DOWN=Navigate  SEL=Select  BACK=Exit");
    } else if (menuOption != prevMenuOption) {
        clearContentArea();
    }
    
    for (int i = 0; i < menuCount; i++) {
        int y = 50 + (i * 40);
        if (i == menuOption) {
            tft.fillRoundRect(10, y - 5, 220, 30, 5, COLOR_SELECT);
            tft.setTextColor(COLOR_BG);
        } else {
            tft.setTextColor(COLOR_TEXT);
        }
        tft.setTextSize(2);
        tft.setCursor(20, y + 5);
        switch(i) {
            case 0: tft.print("Send Reply"); break;
            case 1: tft.print("Delete Message"); break;
            case 2: tft.print("Clear All"); break;
            case 3: tft.print("Sound Settings"); break;
            case 4: tft.print("Reconnect WiFi"); break;
        }
    }
    
    prevMenuOption = menuOption;
}

void executeMenuOption() {
    switch(menuOption) {
        case 0:
            inReplyMenu = true;
            selectedReplyIndex = 0;
            drawReplyMenu();
            break;
        case 1:
            quickDeleteCurrentMessage();
            inMenu = false;
            break;
        case 2:
            if (messageCount > 0) {
                messageCount = 0;
                currentMessageIndex = 0;
                scrollPosition = 0;
                unreadCount = 0;
                quickBeep();
                currentScreen = SCREEN_MESSAGES;
                contentDirty = true;
                headerDirty = true;
                uiNeedsUpdate = true;
                showNotification("All cleared", COLOR_WARNING, 1000);
            }
            inMenu = false;
            break;
        case 3:
            inSoundMenu = true;
            soundMenuOption = 0;
            drawSoundMenu();
            break;
        case 4:
            inMenu = false;
            reconnectWiFi();
            currentScreen = SCREEN_MESSAGES;
            contentDirty = true;
            headerDirty = true;
            uiNeedsUpdate = true;
            break;
    }
}

// ==================== QUICK DELETE FUNCTION ====================
void quickDeleteCurrentMessage() {
    if (messageCount == 0) {
        showNotification("No messages", COLOR_WARNING, 1000);
        return;
    }
    
    for (int i = currentMessageIndex; i < messageCount - 1; i++) {
        messageQueue[i] = messageQueue[i + 1];
    }
    messageCount--;
    
    if (messageCount == 0) {
        currentMessageIndex = 0;
    } else if (currentMessageIndex >= messageCount) {
        currentMessageIndex = messageCount - 1;
    }
    if (currentMessageIndex < 0 && messageCount > 0) {
        currentMessageIndex = 0;
    }
    
    scrollPosition = 0;
    updateUnreadCount();
    quickBeep();
    currentScreen = SCREEN_MESSAGES;
    contentDirty = true;
    headerDirty = true;
    uiNeedsUpdate = true;
    showNotification("Message deleted", COLOR_WARNING, 1000);
}

// ==================== DISPLAY FUNCTIONS ====================
void updateUnreadCount() {
    unreadCount = 0;
    for (int i = 0; i < messageCount; i++)
        if (!messageQueue[i].read) unreadCount++;
}

void drawHeader(String title, uint16_t color) {
    tft.fillRect(0, 0, 240, 30, color);
    tft.setTextColor(COLOR_BG);
    tft.setTextSize(1);
    tft.setCursor(5, 8);
    tft.print(title);
    
    if (currentMode == MODE_STA && wifiConnected) {
        tft.fillCircle(210, 15, 5, COLOR_ACCENT);
    } else if (currentMode == MODE_AP) {
        tft.fillCircle(210, 15, 5, COLOR_WARNING);
    } else {
        tft.fillCircle(210, 15, 5, COLOR_WARNING);
    }
    
    if (unreadCount > 0) {
        tft.fillCircle(100, 15, 8, COLOR_WARNING);
        tft.setTextColor(COLOR_BG);
        tft.setCursor(95, 11);
        tft.print(unreadCount);
    }
    
    if (messageCount > 0) {
        tft.setCursor(140, 8);
        tft.print(currentMessageIndex + 1);
        tft.print("/");
        tft.print(messageCount);
    }
}

void drawFooter() {
    tft.fillRect(0, 220, 240, 20, 0x2104);
    tft.setTextColor(COLOR_TEXT);
    tft.setTextSize(1);
    tft.setCursor(5, 226);
    tft.print("UP/DOWN");
    tft.setCursor(80, 226);
    tft.print("MENU");
    tft.setCursor(130, 226);
    tft.print("DEL");
    tft.setCursor(180, 226);
    tft.print("PWR");
}

void drawMessageContent() {
    if (messageCount == 0) {
        tft.fillRect(0, 30, 240, 190, COLOR_BG);
        tft.setTextColor(COLOR_TEXT);
        tft.setCursor(50, 100);
        tft.print("No messages");
        tft.setCursor(40, 130);
        tft.print("Send from web");
        
        if (currentMode == MODE_STA) {
            tft.setCursor(30, 160);
            tft.print("IP: ");
            tft.print(localIP);
        } else {
            tft.setCursor(30, 160);
            tft.print("Offline Mode: ");
            tft.print(ap_ssid);
        }
        return;
    }
    
    if (messageCount == 49) {
        showNotification("Memory almost full!", COLOR_WARNING, 1500);
    }
    
    if (!messageQueue[currentMessageIndex].read) {
        messageQueue[currentMessageIndex].read = true;
        updateUnreadCount();
    }
    
    String wrappedText = wrapText(messageQueue[currentMessageIndex].text, 21);
    String lines[50];
    int lineCnt = 0;
    int startIdx = 0, endIdx = wrappedText.indexOf('\n');
    
    while (endIdx != -1 && lineCnt < 50) {
        lines[lineCnt++] = wrappedText.substring(startIdx, endIdx);
        startIdx = endIdx + 1;
        endIdx = wrappedText.indexOf('\n', startIdx);
    }
    if (startIdx < wrappedText.length() && lineCnt < 50)
        lines[lineCnt++] = wrappedText.substring(startIdx);
    
    int maxScroll = max(0, lineCnt - visibleLines);
    scrollPosition = constrain(scrollPosition, 0, maxScroll);
    
    tft.fillRect(0, 30, 240, 190, COLOR_BG);
    
    if (lineCnt > visibleLines) {
        int barHeight = (visibleLines * 190) / lineCnt;
        int barY = 30 + (scrollPosition * 190) / lineCnt;
        tft.fillRect(235, barY, 3, barHeight, COLOR_SELECT);
    }
    
    for (int i = scrollPosition; i < min(scrollPosition + visibleLines, lineCnt); i++) {
        int y = 55 + ((i - scrollPosition) * 22);
        
        uint16_t bgColor = (i % 2 == 0) ? 0x1082 : COLOR_BG;
        if (messageQueue[currentMessageIndex].priority) {
            bgColor = 0x8000;
        }
        
        tft.fillRect(5, y - 2, 230, 20, bgColor);
        tft.setTextColor(messageQueue[currentMessageIndex].priority ? COLOR_WARNING : COLOR_TEXT);
        tft.setCursor(8, y);
        tft.print(lines[i]);
    }
    
    tft.setTextSize(1);
    tft.setTextColor(COLOR_INFO);
    tft.setCursor(5, 33);
    tft.print(messageQueue[currentMessageIndex].timestamp);
    
    if (messageQueue[currentMessageIndex].priority) {
        tft.setTextColor(COLOR_WARNING);
        tft.setCursor(180, 33);
        tft.print("!");
    }
}

void drawHomeScreen() {
    currentScreen = SCREEN_HOME;
    tft.fillScreen(COLOR_BG);
    tft.setTextColor(COLOR_ACCENT);
    tft.setTextSize(3);
    tft.setCursor(35, 60);
    tft.print("Smart");
    tft.setTextColor(COLOR_INFO);
    tft.setCursor(125, 60);
    tft.print("Comm");
    tft.setTextColor(COLOR_TEXT);
    tft.setTextSize(1);
    tft.setCursor(55, 95);
    tft.print("Chat System");
    
    tft.fillRoundRect(20, 120, 200, 80, 5, 0x2104);
    tft.drawRoundRect(20, 120, 200, 80, 5, COLOR_ACCENT);
    
    if (currentMode == MODE_STA && wifiConnected) {
        tft.setTextColor(COLOR_ACCENT);
        tft.setCursor(30, 135);
        tft.print("Connected to:");
        tft.setTextColor(COLOR_TEXT);
        tft.setCursor(30, 155);
        tft.print(home_ssid);
        tft.setCursor(30, 175);
        tft.print("IP: ");
        tft.print(localIP);
    } else if (currentMode == MODE_STA && !wifiConnected) {
        tft.setTextColor(COLOR_WARNING);
        tft.setCursor(30, 135);
        tft.print("WiFi Failed!");
        tft.setTextColor(COLOR_TEXT);
        tft.setCursor(30, 155);
        tft.print("Using Offline Mode");
        tft.setCursor(30, 175);
        tft.print("SSID: ");
        tft.print(ap_ssid);
    } else {
        tft.setTextColor(COLOR_WARNING);
        tft.setCursor(30, 135);
        tft.print("Offline Mode Active");
        tft.setTextColor(COLOR_TEXT);
        tft.setCursor(30, 155);
        tft.print("SSID: ");
        tft.print(ap_ssid);
        tft.setCursor(30, 175);
        tft.print("IP: ");
        tft.print(localIP);
    }
}

// ==================== BUTTON HANDLING ====================
void checkButtons() {
    unsigned long now = millis();
    if (now - lastButtonCheck < 20) return;
    lastButtonCheck = now;
    
    bool currentUp = digitalRead(BTN_UP);
    bool currentDown = digitalRead(BTN_DOWN);
    bool currentSelect = digitalRead(BTN_SELECT);
    bool currentBack = digitalRead(BTN_BACK);
    bool currentDelete = digitalRead(BTN_DELETE);
    bool currentPower = digitalRead(BTN_POWER);
    
    if (currentUp == LOW || currentDown == LOW || currentSelect == LOW || 
        currentBack == LOW || currentDelete == LOW || currentPower == LOW) {
        lastActivityTime = now;
        powerState = POWER_ACTIVE;
        uiIdle = false;
    }
    
    if (lastUpState == HIGH && currentUp == LOW) {
        if (now - lastButtonPress > debounceDelay) {
            lastButtonPress = now;
            if (inReplyMenu) {
                selectedReplyIndex = (selectedReplyIndex - 1 + replyCount) % replyCount;
                drawReplyMenu();
            } else if (inSoundMenu) {
                soundMenuOption = (soundMenuOption - 1 + 3) % 3;
                drawSoundMenu();
            } else if (inMenu) {
                menuOption = (menuOption - 1 + menuCount) % menuCount;
                drawMainMenu();
            } else if (scrollPosition > 0) {
                scrollPosition--;
                contentDirty = true;
                uiNeedsUpdate = true;
            }
        }
    }
    
    if (lastDownState == HIGH && currentDown == LOW) {
        if (now - lastButtonPress > debounceDelay) {
            lastButtonPress = now;
            if (inReplyMenu) {
                selectedReplyIndex = (selectedReplyIndex + 1) % replyCount;
                drawReplyMenu();
            } else if (inSoundMenu) {
                soundMenuOption = (soundMenuOption + 1) % 3;
                drawSoundMenu();
            } else if (inMenu) {
                menuOption = (menuOption + 1) % menuCount;
                drawMainMenu();
            } else if (messageCount > 0) {
                String wrappedText = wrapText(messageQueue[currentMessageIndex].text, 21);
                int lineCnt = 1;
                for (int i = 0; i < wrappedText.length(); i++)
                    if (wrappedText.charAt(i) == '\n') lineCnt++;
                int maxScroll = max(0, lineCnt - visibleLines);
                if (scrollPosition < maxScroll) {
                    scrollPosition++;
                    contentDirty = true;
                    uiNeedsUpdate = true;
                }
            }
        }
    }
    
    if (lastSelectState == HIGH && currentSelect == LOW) {
        if (now - lastButtonPress > debounceDelay) {
            lastButtonPress = now;
            if (inReplyMenu) {
                sendSelectedReply();
            } else if (inSoundMenu) {
                handleSoundMenu();
            } else if (inMenu) {
                executeMenuOption();
            } else {
                inMenu = true;
                menuOption = 0;
                drawMainMenu();
            }
        }
    }
    
    if (lastBackState == HIGH && currentBack == LOW) {
        if (now - lastButtonPress > debounceDelay) {
            lastButtonPress = now;
            if (inReplyMenu) {
                inReplyMenu = false;
                currentScreen = SCREEN_MESSAGES;
                contentDirty = true;
                headerDirty = true;
                uiNeedsUpdate = true;
                showNotification("Cancelled", COLOR_INFO, 500);
            } else if (inSoundMenu) {
                inSoundMenu = false;
                inMenu = true;
                drawMainMenu();
            } else if (inMenu) {
                inMenu = false;
                currentScreen = SCREEN_MESSAGES;
                contentDirty = true;
                headerDirty = true;
                uiNeedsUpdate = true;
            }
        }
    }
    
    if (lastDeleteState == HIGH && currentDelete == LOW) {
        if (now - lastButtonPress > debounceDelay) {
            lastButtonPress = now;
            if (!inMenu && !inReplyMenu && !inSoundMenu) {
                quickDeleteCurrentMessage();
            }
        }
    }
    
    if (lastPowerState == HIGH && currentPower == LOW) {
        if (now - lastButtonPress > debounceDelay) {
            lastButtonPress = now;
            screenOn = !screenOn;
            updateBacklight();
            if (screenOn) {
                currentScreen = SCREEN_MESSAGES;
                contentDirty = true;
                headerDirty = true;
                uiNeedsUpdate = true;
                uiIdle = false;
            }
        }
    }
    
    lastUpState = currentUp;
    lastDownState = currentDown;
    lastSelectState = currentSelect;
    lastBackState = currentBack;
    lastDeleteState = currentDelete;
    lastPowerState = currentPower;
}

// ==================== MESSAGE HANDLING ====================
void addNewMessage(String text) {
    bool isPriority = text.startsWith("!");
    if (isPriority) {
        text = text.substring(1);
    }
    
    if (messageCount < 50) {
        messageQueue[messageCount].text = text;
        messageQueue[messageCount].timestamp = getTimestamp();
        messageQueue[messageCount].read = false;
        messageQueue[messageCount].priority = isPriority;
        messageCount++;
        currentMessageIndex = messageCount - 1;
        scrollPosition = 0;
    } else {
        for (int i = 0; i < 49; i++) messageQueue[i] = messageQueue[i + 1];
        messageQueue[49].text = text;
        messageQueue[49].timestamp = getTimestamp();
        messageQueue[49].read = false;
        messageQueue[49].priority = isPriority;
        currentMessageIndex = 49;
        scrollPosition = 0;
    }
    
    cachedMessageIndex = -1;
    cachedWrappedText = "";
    
    updateUnreadCount();
    currentScreen = SCREEN_MESSAGES;
    contentDirty = true;
    headerDirty = true;
    uiNeedsUpdate = true;
    uiIdle = false;
    lastActivityTime = millis();
    
    if (isPriority) {
        playPriorityAlert();
    } else if (text.startsWith("[REPLY]")) {
        playReplyTone();
    } else {
        playNormalBeep();
    }
    
    tft.fillRect(0, 0, 240, 240, isPriority ? COLOR_WARNING : COLOR_ACCENT);
    delay(50);
    contentDirty = true;
    uiNeedsUpdate = true;
}

void updateDisplay(String text) {
    if (text.length() > 0) addNewMessage(text);
}

// ==================== WEB SERVER ====================
void handleMessages() {
    String json = "[";
    
    for (int i = 0; i < messageCount; i++) {
        if (i > 0) json += ",";
        
        json += "{";
        json += "\"text\":\"" + messageQueue[i].text + "\",";
        json += "\"time\":\"" + messageQueue[i].timestamp + "\",";
        json += "\"priority\":" + String(messageQueue[i].priority ? "true" : "false");
        json += "}";
    }
    
    json += "]";
    server.send(200, "application/json", json);
}

void handleRoot() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Smart Comm Chat System</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
            padding: 20px;
        }
        .container {
            background: white;
            border-radius: 30px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            width: 100%;
            max-width: 600px;
            height: 90vh;
            display: flex;
            flex-direction: column;
            overflow: hidden;
        }
        .header {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 20px;
            text-align: center;
        }
        .header h1 { font-size: 24px; margin-bottom: 5px; }
        .status { font-size: 12px; opacity: 0.9; }
        .chat-box {
            flex: 1;
            overflow-y: auto;
            padding: 20px;
            background: #f5f5f5;
            display: flex;
            flex-direction: column;
        }
        .msg {
            max-width: 70%;
            margin-bottom: 15px;
            padding: 10px 15px;
            border-radius: 18px;
            position: relative;
            word-wrap: break-word;
        }
        .sent {
            background: #667eea;
            color: white;
            align-self: flex-end;
            border-bottom-right-radius: 4px;
        }
        .received {
            background: white;
            color: #333;
            align-self: flex-start;
            border-bottom-left-radius: 4px;
            box-shadow: 0 1px 2px rgba(0,0,0,0.1);
        }
        .priority {
            background: #ff4444;
            color: white;
            align-self: flex-start;
            border-bottom-left-radius: 4px;
        }
        .time {
            font-size: 10px;
            opacity: 0.7;
            margin-top: 5px;
        }
        .input-area {
            padding: 20px;
            background: white;
            border-top: 1px solid #e0e0e0;
            display: flex;
            gap: 10px;
        }
        textarea {
            flex: 1;
            padding: 10px;
            border: 2px solid #e0e0e0;
            border-radius: 25px;
            font-family: inherit;
            font-size: 14px;
            resize: none;
            outline: none;
        }
        textarea:focus {
            border-color: #667eea;
        }
        button {
            padding: 10px 20px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            border: none;
            border-radius: 25px;
            font-size: 14px;
            font-weight: bold;
            cursor: pointer;
            transition: transform 0.2s;
        }
        button:hover {
            transform: scale(0.98);
        }
        .reply-badge {
            font-size: 10px;
            margin-left: 5px;
            opacity: 0.8;
        }
        @keyframes fadeIn {
            from { opacity: 0; transform: translateY(10px); }
            to { opacity: 1; transform: translateY(0); }
        }
        .msg {
            animation: fadeIn 0.3s ease;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>WiFi Pager Chat</h1>
            <div class="status" id="status">Connecting...</div>
        </div>
        
        <div class="chat-box" id="chatBox"></div>
        
        <div class="input-area">
            <textarea id="message" placeholder="Type your message... (Start with ! for priority)" rows="2"></textarea>
            <button onclick="sendMessage()">Send</button>
        </div>
    </div>

    <script>
        let lastMessageCount = 0;
        
        async function sendMessage() {
            const msg = document.getElementById("message").value.trim();
            if (!msg) return;
            
            const response = await fetch('/send', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: 'message=' + encodeURIComponent(msg)
            });
            
            if (response.ok) {
                document.getElementById("message").value = "";
                loadMessages();
            }
        }
        
        async function loadMessages() {
            try {
                const res = await fetch('/messages');
                const data = await res.json();
                
                const chatBox = document.getElementById("chatBox");
                const currentMessageCount = data.length;
                
                const shouldScroll = currentMessageCount !== lastMessageCount;
                lastMessageCount = currentMessageCount;
                
                chatBox.innerHTML = "";
                
                data.forEach(m => {
                    const div = document.createElement("div");
                    let displayText = m.text;
                    let isReply = false;
                    let isPriority = m.priority;
                    
                    if (m.text.startsWith("[REPLY]")) {
                        displayText = m.text.replace("[REPLY] ", "");
                        isReply = true;
                    }
                    
                    if (isPriority && !isReply) {
                        div.className = "msg priority";
                    } else if (isReply) {
                        div.className = "msg received";
                    } else {
                        div.className = "msg sent";
                    }
                    
                    div.innerHTML = `
                        ${escapeHtml(displayText)}
                        ${isReply ? '<span class="reply-badge">[Device]</span>' : ''}
                        <div class="time">${m.time}</div>
                    `;
                    
                    chatBox.appendChild(div);
                });
                
                if (shouldScroll) {
                    chatBox.scrollTop = chatBox.scrollHeight;
                }
                
                const statusElem = document.getElementById("status");
                if (data.length > 0) {
                    statusElem.innerHTML = data.length + " messages | Last: " + data[data.length-1].time;
                } else {
                    statusElem.innerHTML = "No messages yet | Send a message!";
                }
            } catch(e) {
                console.error('Error loading messages:', e);
                document.getElementById("status").innerHTML = "Connection error";
            }
        }
        
        function escapeHtml(text) {
            const div = document.createElement('div');
            div.textContent = text;
            return div.innerHTML;
        }
        
        document.getElementById("message").addEventListener("keypress", function(e) {
            if (e.key === "Enter" && !e.shiftKey) {
                e.preventDefault();
                sendMessage();
            }
        });
        
        setInterval(loadMessages, 2000);
        loadMessages();
        
        document.addEventListener("visibilitychange", function() {
            if (!document.hidden) {
                loadMessages();
            }
        });
    </script>
</body>
</html>
)rawliteral";
    
    String htmlWithStatus = String(html);
    server.send(200, "text/html", htmlWithStatus);
}

void handleSend() {
    if (server.hasArg("message")) {
        String message = server.arg("message");
        if (message.length() > 0) {
            updateDisplay(message);
            Serial.print("Message received: ");
            Serial.println(message);
        }
        server.send(200, "text/plain", "OK");
    } else {
        server.send(400, "text/plain", "No message");
    }
}

void handleClear() {
    messageCount = 0;
    currentMessageIndex = 0;
    scrollPosition = 0;
    unreadCount = 0;
    cachedMessageIndex = -1;
    cachedWrappedText = "";
    currentScreen = SCREEN_MESSAGES;
    contentDirty = true;
    headerDirty = true;
    uiNeedsUpdate = true;
    server.send(200, "text/plain", "Cleared");
}

void handleStatus() {
    String json = "{";
    json += "\"messages\":" + String(messageCount) + ",";
    json += "\"unread\":" + String(unreadCount);
    json += "}";
    server.send(200, "application/json", json);
}

void handleNotFound() {
    server.send(404, "text/plain", "Not found");
}

// ==================== SETUP ====================
void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== Smart Comm Chat System Starting ===");
    
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    
    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_SELECT, INPUT_PULLUP);
    pinMode(BTN_BACK, INPUT_PULLUP);
    pinMode(BTN_DELETE, INPUT_PULLUP);
    pinMode(BTN_POWER, INPUT_PULLUP);
    Serial.println("Buttons initialized");
    
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    
    SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
    tft.init(240, 240);
    tft.setRotation(1);
    tft.setTextWrap(false);
    visibleLines = 7;
    
    drawHomeScreen();
    delay(2000);
    
    if (connectToWiFi()) {
        currentMode = MODE_STA;
        wifiConnected = true;
        Serial.print("Connected! IP: ");
        Serial.println(localIP);
    } else {
        startAccessPoint();
        currentMode = MODE_AP;
        wifiConnected = false;
        Serial.print("AP Mode! IP: ");
        Serial.println(localIP);
        showNotification("WiFi Failed! Using AP Mode", COLOR_WARNING, 3000);
    }
    
    drawHomeScreen();
    delay(2000);
    
    server.on("/", handleRoot);
    server.on("/send", HTTP_POST, handleSend);
    server.on("/clear", HTTP_POST, handleClear);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/messages", HTTP_GET, handleMessages);
    server.onNotFound(handleNotFound);
    server.begin();
    
    quickBeep();
    if (currentMode == MODE_STA && wifiConnected) {
        showNotification("Chat System Ready!", COLOR_ACCENT, 2000);
        Serial.println("Chat System Ready!");
    } else {
        showNotification("AP Mode: " + String(ap_ssid), COLOR_WARNING, 3000);
    }
    Serial.print("Server started at IP: ");
    Serial.println(localIP);
    
    lastActivityTime = millis();
}

void loop() {
    server.handleClient();
    checkButtons();
    clearNotification();
    updateBacklight();
    checkWiFiConnection();
    
    if (uiNeedsUpdate && millis() - lastUIUpdate > UI_REFRESH_INTERVAL) {
        updateUI();
        uiNeedsUpdate = false;
        lastUIUpdate = millis();
    }
    
    unsigned long idleTime = millis() - lastActivityTime;
    if (idleTime > 5000 && !uiIdle) {
        uiIdle = true;
    }
    
    if (idleTime > sleepTimeout && 
        powerState != POWER_SLEEP && 
        unreadCount == 0 &&
        !inMenu && 
        !inReplyMenu && 
        !inSoundMenu) {
        powerState = POWER_SLEEP;
        goToSleep();
        powerState = POWER_ACTIVE;
        lastActivityTime = millis();
    }
    
    delay(5);
}
