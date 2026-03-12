# JB74-TPMS

```
CC1101 →  nRF52840
------------------------
VCC    → 3.3V  (важно: не 5V!)
GND    → GND
CSN    → D7   (PIN_CS)
SCLK   → D8   (SCK)
MOSI   → D10  (MOSI)
MISO   → D9   (MISO)
GDO0   → D2   (прерывание)
```

```
adafruit-nrfutil dfu serial -pkg /home/odity/.cache/arduino/sketches/688358114825BA110B7C0F3D6D14D40D/TPMS.ino.zip -p /dev/ttyACM0 -b 115200
```

```
Upgrading target on /dev/ttyACM0 with DFU package /home/odity/.cache/arduino/sketches/688358114825BA110B7C0F3D6D14D40D/TPMS.ino.zip. Flow control is disabled, Dual bank, Touch disabled
########################################
########################################
########################################
####################################
Activating new firmware
Device programmed.
viva:odity # cat /dev/ttyACM0                                                                                                                            


==========================================

    TPMS УНИВЕРСАЛЬНЫЙ СКАНЕР

==========================================

Поиск датчиков на 433 МГц и 315 МГц

Автоматическое переключение каждые 3 сек


Ошибка CC1101! Проверь подключение
```
