/*
 * TPMS сканер для nRF52840 + CC1101
 * УНИВЕРСАЛЬНАЯ ВЕРСИЯ - ищет датчики на 433 и 315 МГц
 * Автоматическое переключение частот
 */

#include <Adafruit_TinyUSB.h>
#include <RadioLib.h>

// Пины для XIAO nRF52840
#define PIN_SCLK   8
#define PIN_MOSI   10
#define PIN_MISO   9
#define PIN_CS     7
#define PIN_GDO0   2
#define LED_PIN    LED_BUILTIN

// Частоты для сканирования
const float FREQUENCIES[] = {433.92, 315.0};  // 433 МГц и 315 МГц
const int FREQ_COUNT = 2;
int currentFreq = 0;  // Индекс текущей частоты

// SPI и радио
SPIClass mySPI(NRF_SPIM0, PIN_SCLK, PIN_MOSI, PIN_MISO);
Module myModule(PIN_CS, PIN_GDO0, RADIOLIB_NC, RADIOLIB_NC, mySPI);
CC1101 radio = CC1101(&myModule);

// Структура датчика
struct TPMSData {
  uint32_t id;
  float pressure;
  float temperature;
  unsigned long lastSeen;
  int rssi;
  int count;
  float frequency;  // Запоминаем, на какой частоте найден
};

TPMSData sensors[16];  // Увеличили до 16 датчиков
int sensorCount = 0;

// Для переключения частот
unsigned long lastFreqSwitch = 0;
const int FREQ_SWITCH_TIME = 3000;  // 3 секунды на частоте

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  Serial.begin(115200);
  delay(2000);
  while(!Serial) delay(100);
  
  Serial.println("\n\n==========================================");
  Serial.println("    TPMS УНИВЕРСАЛЬНЫЙ СКАНЕР");
  Serial.println("==========================================");
  Serial.println("Поиск датчиков на 433 МГц и 315 МГц");
  Serial.println("Автоматическое переключение каждые 3 сек\n");
  
  // SPI
  mySPI.begin();
  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  
  // Инициализация CC1101
  if(!initCC1101(FREQUENCIES[currentFreq])) {
    Serial.println("Ошибка CC1101! Проверь подключение");
    while(1) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(500);
    }
  }
  
  Serial.println("\nПоиск начат...\n");
  
  // Сигнал готовности
  for(int i=0; i<3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }
}

bool initCC1101(float freq) {
  mySPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  
  int state = radio.begin(freq);
  if(state == RADIOLIB_ERR_NONE) {
    radio.setOOK(true);
    radio.setRxBandwidth(200.0);
    radio.startReceive();
  }
  
  mySPI.endTransaction();
  
  if(state == RADIOLIB_ERR_NONE) {
    Serial.print("📡 Частота: ");
    Serial.print(freq);
    Serial.println(" МГц");
    return true;
  }
  return false;
}

void switchFrequency() {
  // Переключаем на следующую частоту
  currentFreq = (currentFreq + 1) % FREQ_COUNT;
  
  Serial.print("\n🔄 Переключение на ");
  Serial.print(FREQUENCIES[currentFreq]);
  Serial.println(" МГц...");
  
  mySPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  radio.setFrequency(FREQUENCIES[currentFreq]);
  radio.startReceive();
  mySPI.endTransaction();
}

void loop() {
  byte data[64];
  
  // Переключение частот каждые 3 секунды
  if(millis() - lastFreqSwitch > FREQ_SWITCH_TIME) {
    lastFreqSwitch = millis();
    switchFrequency();
  }
  
  // Чтение данных
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
  
  // Вывод каждые 10 секунд
  static unsigned long lastPrint = 0;
  if(millis() - lastPrint > 10000) {
    lastPrint = millis();
    printSensors();
  }
  
  delay(10);
}

void processPacket(byte* data, int len, int rssi, float freq) {
  if(len < 4) return;
  
  // Пробуем разные способы извлечения ID
  uint32_t id = 0;
  
  // Способ 1: первые 4 байта
  if(len >= 4) {
    id = (uint32_t)data[0] << 24 | data[1] << 16 | data[2] << 8 | data[3];
  }
  
  // Способ 2: если ID выглядит странно, пробуем другие комбинации
  if(id == 0 || id == 0xFFFFFFFF) {
    if(len >= 3) {
      id = data[0] << 16 | data[1] << 8 | data[2];
    }
  }
  
  if(id == 0 || id == 0xFFFFFFFF) return;
  
  // Ищем или добавляем датчик
  bool found = false;
  for(int i=0; i<sensorCount; i++) {
    if(sensors[i].id == id) {
      sensors[i].lastSeen = millis();
      sensors[i].rssi = rssi;
      sensors[i].count++;
      sensors[i].frequency = freq;  // Обновляем частоту
      
      // Парсим данные (разные форматы)
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
    Serial.print(" на ");
    Serial.print(freq);
    Serial.print(" МГц, RSSI: ");
    Serial.print(rssi);
    Serial.println(" dBm");
  }
}

void decodeSensorData(byte* data, int len, int index) {
  // Пробуем разные форматы данных
  
  // Формат 1: давление в байтах 4-5 (для многих датчиков)
  if(len >= 6) {
    int rawP = (data[4] << 8) | data[5];
    if(rawP > 0 && rawP < 10000) {
      sensors[index].pressure = rawP * 0.0625;  // PSI
    }
    
    // Температура в байтах 6-7
    if(len >= 8) {
      int rawT = (data[6] << 8) | data[7];
      if(rawT > 0 && rawT < 1000) {
        sensors[index].temperature = (rawT * 0.1) - 50;  // °C
      }
    }
  }
  
  // Формат 2: давление в байтах 2-3 (для некоторых китайских)
  if(sensors[index].pressure == 0 && len >= 4) {
    int rawP2 = (data[2] << 8) | data[3];
    if(rawP2 > 0 && rawP2 < 1000) {
      sensors[index].pressure = rawP2 * 0.1;
    }
  }
}

void printSensors() {
  Serial.println("\n┌─────────────────────────────────────────────────────────────────┐");
  Serial.println("│                     TPMS ДАТЧИКИ (универсальный поиск)           │");
  Serial.println("├───────────┬───────────┬──────────┬───────────┬──────────┬─────────┤");
  Serial.println("│ ID (HEX)  │ Давление  │ Темп(°C) │ RSSI(dBm) │ Частота  │ Сигналов│");
  Serial.println("├───────────┼───────────┼──────────┼───────────┼──────────┼─────────┤");
  
  if(sensorCount == 0) {
    Serial.println("│                   Нет датчиков (сканирование...)                  │");
  } else {
    for(int i=0; i<sensorCount; i++) {
      // ID (последние 8 символов)
      Serial.print("│ ");
      Serial.print(sensors[i].id, HEX);
      Serial.print(" │ ");
      
      // Давление
      if(sensors[i].pressure > 0) {
        Serial.print(sensors[i].pressure, 1);
        Serial.print(" psi  ");
      } else {
        Serial.print(" --    ");
      }
      
      // Температура
      if(sensors[i].temperature > -30) {
        Serial.print(" ");
        Serial.print(sensors[i].temperature, 1);
        Serial.print(" C  ");
      } else {
        Serial.print("  --  ");
      }
      
      // RSSI
      if(sensors[i].rssi != 0) {
        Serial.print(" ");
        Serial.print(sensors[i].rssi);
        Serial.print("    ");
      } else {
        Serial.print(" ---   ");
      }
      
      // Частота
      Serial.print(" ");
      Serial.print(sensors[i].frequency, 0);
      Serial.print(" МГц ");
      
      // Счетчик
      Serial.print("  ");
      Serial.print(sensors[i].count);
      
      // Маркер свежести
      unsigned long age = (millis() - sensors[i].lastSeen) / 1000;
      if(age < 60) Serial.print(" *");
      
      Serial.println(" │");
    }
  }
  
  Serial.println("└───────────┴───────────┴──────────┴───────────┴──────────┴─────────┘");
  Serial.print("📊 Найдено датчиков: ");
  Serial.print(sensorCount);
  Serial.println("  (* - данные свежие < 1 мин)");
  Serial.print("📍 Текущая частота: ");
  Serial.print(FREQUENCIES[currentFreq]);
  Serial.println(" МГц (автопереключение)");
}