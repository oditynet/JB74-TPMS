/*
 * TPMS сканер для nRF52840 + CC1101 + OLED
 * РАБОЧАЯ ВЕРСИЯ с одновременной работой SPI и I2C
 */

#include <Adafruit_TinyUSB.h>
#include <RadioLib.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Пины для XIAO nRF52840
#define PIN_SCLK   8
#define PIN_MOSI   10
#define PIN_MISO   9
#define PIN_CS     7
#define PIN_GDO0   2
#define LED_PIN    LED_BUILTIN

// Пины OLED
#define OLED_SDA   20
#define OLED_SCL   21
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3D  // Ваш адрес

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ВАЖНО: Используем SPIM1 вместо SPIM0 (чтобы не конфликтовать с I2C)
SPIClass mySPI(NRF_SPIM1, PIN_SCLK, PIN_MOSI, PIN_MISO);
Module myModule(PIN_CS, PIN_GDO0, RADIOLIB_NC, RADIOLIB_NC, mySPI);
CC1101 radio = CC1101(&myModule);

// Частоты для сканирования
const float FREQUENCIES[] = {433.92, 315.0};
const int FREQ_COUNT = 2;
int currentFreq = 0;

// Структура датчика
struct TPMSData {
  uint32_t id;
  float pressure;
  float temperature;
  unsigned long lastSeen;
  int rssi;
  int count;
  float frequency;
};

TPMSData sensors[16];
int sensorCount = 0;

// Для переключения частот
unsigned long lastFreqSwitch = 0;
const int FREQ_SWITCH_TIME = 3000;

// Для обновления дисплея
unsigned long lastDisplayUpdate = 0;
const int DISPLAY_UPDATE_INTERVAL = 1000;

void setup() {
  Serial.begin(115200);
  delay(300);  // Даем время на подключение монитора порта
  
  Serial.println("\n\n==================================");
  Serial.println("  TPMS СКАНЕР + OLED + CC1101");
  Serial.println("==================================\n");
  
  Wire.setPins(OLED_SDA, OLED_SCL);
  Wire.begin();
  delay(100);
  
  // Сканируем I2C шину
  Serial.println("Сканирование I2C устройств:");
  int devicesFound = 0;
  for(byte addr=1; addr<127; addr++) {
    Wire.beginTransmission(addr);
    if(Wire.endTransmission() == 0) {
      devicesFound++;
      Serial.print("  Найден устройство по адресу 0x");
      if(addr<16) Serial.print("0");
      Serial.println(addr, HEX);
    }
  }
  
  if(devicesFound == 0) {
    Serial.println("  I2C устройств НЕ НАЙДЕНО!");
  }
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED INIT FAILED!");
    for(;;);
  } else {

    // Тестовый вывод
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("OLED Works!");
    display.display();
    delay(1000);
  }
  
  // Приветственный экран
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(20, 10);
  display.println(F("TPMS"));
  display.setCursor(15, 35);
  display.println(F("START"));
  display.display();
  delay(2000);
  
  // ===== 2. ПОТОМ SPI (CC1101) на SPIM1 =====
  Serial.println("\nИнициализация CC1101...");
  
  // Явно указываем, какой SPI блок используем (уже сделано при создании объекта)
  mySPI.begin();
  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  
  // Небольшая задержка для стабилизации
  delay(100);
  
  if(!initCC1101(FREQUENCIES[currentFreq])) {
    Serial.println("CC1101 ERROR! Проверь подключение");
    
    // Показываем ошибку на экране
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(20, 5);
    display.println(F("ERROR"));
    display.setTextSize(1);
    display.setCursor(0, 30);
    display.println(F("CC1101 not found"));
    display.setCursor(0, 42);
    display.println(F("Check wiring"));
    display.setCursor(0, 54);
   // display.println(F("Press RESET"));
    display.display();
    
    delay(2000);
    
    while(1) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(500);
    }
  }
  
  Serial.println("CC1101 OK");
  
  // Финальное сообщение
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("TPMS Scanner"));
  display.print(F("Freq: "));
  display.print(FREQUENCIES[currentFreq], 0);
  display.println(F(" MHz"));
  display.println(F("Ready"));
  display.display();
  delay(1000);
  
  Serial.println("\n✅ ГОТОВ К РАБОТЕ\n");
  
  // Сигнал готовности
  for(int i=0; i<3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }
}

bool initCC1101(float freq) {
  // Каждая операция с CC1101 должна быть в транзакции
  mySPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  
  int state = radio.begin(freq);
  
  if(state == RADIOLIB_ERR_NONE) {
    radio.setOOK(true);
    radio.setRxBandwidth(200.0);
    radio.startReceive();
  }
  
  mySPI.endTransaction();
  
  if(state == RADIOLIB_ERR_NONE) {
    Serial.print("  Частота: ");
    Serial.println(freq);
    return true;
  }
  return false;
}

void switchFrequency() {
  currentFreq = (currentFreq + 1) % FREQ_COUNT;
  
  mySPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  radio.setFrequency(FREQUENCIES[currentFreq]);
  radio.startReceive();
  mySPI.endTransaction();
}

void loop() {
  byte data[64];
  
  // Переключение частот
  if(millis() - lastFreqSwitch > FREQ_SWITCH_TIME) {
    lastFreqSwitch = millis();
    switchFrequency();
  }
  
  // Чтение данных с CC1101
  mySPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  int state = radio.readData(data, 64);
  mySPI.endTransaction();
  
  if(state == RADIOLIB_ERR_NONE) {
    int len = radio.getPacketLength();
    int rssi = radio.getRSSI();
    
    digitalWrite(LED_PIN, HIGH);
    delay(20);
    digitalWrite(LED_PIN, LOW);
    
    processPacket(data, len, rssi, FREQUENCIES[currentFreq]);
    
    mySPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    radio.startReceive();
    mySPI.endTransaction();
  }
  
  // Обновление дисплея (НЕ в транзакции SPI!)
  if(millis() - lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL) {
    lastDisplayUpdate = millis();
    updateDisplay();
  }
  
  // Вывод в Serial каждые 10 секунд
  static unsigned long lastPrint = 0;
  if(millis() - lastPrint > 10000) {
    lastPrint = millis();
    printSensors();
  }
  
  delay(10);
}

void processPacket(byte* data, int len, int rssi, float freq) {
  if(len < 4) return;
  
  // Извлечение ID
  uint32_t id = 0;
  if(len >= 4) {
    id = (uint32_t)data[0] << 24 | data[1] << 16 | data[2] << 8 | data[3];
  }
  
  if(id == 0 || id == 0xFFFFFFFF) {
    if(len >= 3) {
      id = data[0] << 16 | data[1] << 8 | data[2];
    }
  }
  
  if(id == 0 || id == 0xFFFFFFFF) return;
  
  // Поиск или добавление датчика
  bool found = false;
  for(int i=0; i<sensorCount; i++) {
    if(sensors[i].id == id) {
      sensors[i].lastSeen = millis();
      sensors[i].rssi = rssi;
      sensors[i].count++;
      sensors[i].frequency = freq;
      decodeSensorData(data, len, i);
      found = true;
      break;
    }
  }
  
  if(!found && sensorCount < 16) {
    sensors[sensorCount].id = id;
    sensors[sensorCount].lastSeen = millis();
    sensors[sensorCount].rssi = rssi;
    sensors[sensorCount].pressure = 0;
    sensors[sensorCount].temperature = 0;
    sensors[sensorCount].count = 1;
    sensors[sensorCount].frequency = freq;
    
    decodeSensorData(data, len, sensorCount);
    sensorCount++;
    
    Serial.print("\n✅ НОВЫЙ ДАТЧИК! ID: 0x");
    Serial.print(id, HEX);
    Serial.print(" RSSI: ");
    Serial.print(rssi);
    Serial.println(" dBm");
  }
}

void decodeSensorData(byte* data, int len, int index) {
  if(len >= 6) {
    int rawP = (data[4] << 8) | data[5];
    if(rawP > 0 && rawP < 10000) {
      sensors[index].pressure = rawP * 0.0625;
    }
    
    if(len >= 8) {
      int rawT = (data[6] << 8) | data[7];
      if(rawT > 0 && rawT < 1000) {
        sensors[index].temperature = (rawT * 0.1) - 50;
      }
    }
  }
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  // Верхняя строка с частотой
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("TPMS ");
  display.print(FREQUENCIES[currentFreq], 0);
  display.print("MHz");
  
  if(sensorCount == 0) {
    display.setTextSize(1);
    display.setCursor(20, 30);
    display.println("No sensors");
  } else {
    // Отображаем до 4 датчиков
    for(int i=0; i<min(sensorCount, 4); i++) {
      int y = 12 + i*13;
      
      // ID (последние 4 цифры)
      display.setCursor(0, y);
      display.print("ID");
      display.print(i+1);
      display.print(":");
      display.print((sensors[i].id & 0xFFFF), HEX);
      
      // Давление
      if(sensors[i].pressure > 0) {
        display.setCursor(60, y);
        display.print(sensors[i].pressure, 1);
        display.print("psi");
      }
      
      // Маркер свежести
      unsigned long age = (millis() - sensors[i].lastSeen) / 1000;
      if(age < 60) {
        display.setCursor(115, y);
        display.print("*");
      }
    }
  }
  
  display.display();
}

void printSensors() {
  Serial.println("\n--- TPMS ДАТЧИКИ ---");
  for(int i=0; i<sensorCount; i++) {
    Serial.print("ID: 0x");
    Serial.print(sensors[i].id, HEX);
    Serial.print(" Давление: ");
    Serial.print(sensors[i].pressure, 1);
    Serial.print(" psi RSSI: ");
    Serial.print(sensors[i].rssi);
    Serial.println(" dBm");
  }
  Serial.println("--------------------");
}
