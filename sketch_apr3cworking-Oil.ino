#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <UniversalTelegramBot.h>
#include <WiFiClientSecure.h>

// **🛠 Sensor & Motor Pins**
#define FLOW_SENSOR_PIN  34  
#define TRIG_PIN         5  
#define ECHO_PIN         18  
#define RELAY_PIN        23  

// **📟 LCD Display**
LiquidCrystal_I2C lcd(0x27, 16, 2);

// **🛢 Tank Parameters**
const int tankDepth = 100;  

// **📶 Wi-Fi Credentials**
const char* ssid = "JOHN";
const char* password = "password";

// **📢 Telegram Bot Credentials**
#define BOT_TOKEN "8025573533:AAHTASOEhJzLGxrO9mbdGVoZloZjgA8Q_as"
#define CHAT_ID "1285349175"

WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

// **📄 Google Sheets URL**
String GOOGLE_SCRIPT_URL = "https://script.google.com/macros/s/AKfycbzdmUe5N6hxC-mlx6DfIYL_AS7oJclhdFnpNkjRxZBCJaf3bHZebCLB6HTr-HfCSgEy/exec";

// **🌡 Variables**
int oilLevel = 0;
bool motorState = false;
volatile int pulseCount = 0;
float flowRate = 0.0;
bool lowOilAlertSent = false;
bool highOilAlertSent = false;
int requestedOil = 0;  
String consumerName = "N/A";  
String machineNumber = "N/A";  
unsigned long lastTelegramMessage = 0;

// **⏳ ISR for Flow Sensor**
void IRAM_ATTR countPulse() {
    pulseCount++;
}

// **📏 Function to Read Oil Level**
int readOilLevel() {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    
    long duration = pulseIn(ECHO_PIN, HIGH);
    int distance = duration * 0.034 / 2;  
    int level = tankDepth - distance;

    if (level < 0) level = 0;
    return map(level, 0, tankDepth - 3, 0, 100);
}

// **🚰 Function to Calculate Flow Rate (L/min)**
float calculateFlowRate() {
    float frequency = pulseCount / 7.5;  
    pulseCount = 0;
    return frequency;
}

// **📢 Function to Send Telegram Alert**
void sendTelegramAlert(String message) {
    if (millis() - lastTelegramMessage > 10000) {  // Send max 1 message every 10 sec
        Serial.println("🚀 Sending Telegram Message...");
        Serial.println("Message: " + message);

        bool sent = bot.sendMessage(CHAT_ID, message, "");
        
        if (sent) {
            Serial.println("✅ Message Sent Successfully!");
            lastTelegramMessage = millis();
        } else {
            Serial.println("❌ Failed to Send Message!");
        }
    } else {
        Serial.println("⏳ Telegram message skipped (too soon)");
    }
}

void setup() {
    Serial.begin(115200);
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Starting...");

    pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, HIGH);  

    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), countPulse, RISING);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }
    
    Serial.println("\n✅ WiFi Connected!");
    client.setInsecure();  

    sendTelegramAlert("✅ ESP32 Telegram Test Message!");
}

void loop() {
    oilLevel = readOilLevel();
    flowRate = calculateFlowRate();

    // **📟 Display Real-Time Data on LCD**
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Oil: " + String(oilLevel) + "%");
    lcd.setCursor(0, 1);
    lcd.print("Flow: " + String(flowRate) + " L/min");

    // **🛑 Auto-refill logic**
    if (oilLevel < 5 && !motorState) {
        digitalWrite(RELAY_PIN, LOW);
        motorState = true;
        Serial.println("🚨 Motor ON - Low Oil Alert");
        sendTelegramAlert("⚠️ Low Oil! Refilling started.");
    } 
    else if (oilLevel > 98 && motorState) {
        digitalWrite(RELAY_PIN, HIGH);
        motorState = false;
        Serial.println("✅ Motor OFF - Tank Full");
        sendTelegramAlert("✅ Oil Tank Full. Motor Stopped.");
    }

    // **🚰 Dispense oil if requested**
    if (requestedOil > 0) {
        Serial.println("Dispensing Oil...");
        digitalWrite(RELAY_PIN, LOW);
        motorState = true;
        pulseCount = 0;
        int dispensedOil = 0;

        while (dispensedOil < requestedOil) {
            dispensedOil = pulseCount * 1000 / 7.5;
        }

        digitalWrite(RELAY_PIN, HIGH);
        motorState = false;
        sendTelegramAlert("✅ Oil Dispensed: " + String(dispensedOil) + "ml");

        // **📝 Log Data to Google Sheets**
        String url = GOOGLE_SCRIPT_URL + "?consumer=" + consumerName + 
                     "&machine_no=" + machineNumber + 
                     "&req_oil=" + String(requestedOil) + 
                     "&dispensed_oil=" + String(dispensedOil) + 
                     "&oil_level=" + String(oilLevel) + 
                     "&motor_status=" + (motorState ? "ON" : "OFF") +
                     "&flow_rate=" + String(flowRate);

        HTTPClient http;
        http.begin(url);
        http.GET();
        http.end();

        requestedOil = 0;
    }

    // **🌍 Send Data to Google Sheets Every 3 Seconds**
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 3000) {
        String url = GOOGLE_SCRIPT_URL + "?consumer=N/A" + 
                     "&machine_no=N/A" + 
                     "&req_oil=0" +  
                     "&dispensed_oil=0" +  
                     "&oil_level=" + String(oilLevel) + 
                     "&motor_status=" + (motorState ? "ON" : "OFF") +
                     "&flow_rate=" + String(flowRate);

        HTTPClient http;
        http.begin(url);
        http.GET();
        http.end();

        lastUpdate = millis();
    }

    delay(1000);  
}
