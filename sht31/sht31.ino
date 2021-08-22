#include <Adafruit_NeoPixel.h>
#include <Adafruit_SHT31.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <FS.h>
#include <GyverTimer.h>
#include <LittleFS.h>
#include <NTPClient.h>
#include <PubSubClient.h>
#include <UnixTime.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#ifdef __AVR__

#include <avr/power.h>
#endif
#define PIN D3

Adafruit_NeoPixel pixels(1, PIN, NEO_RGB + NEO_KHZ800);
#define DELAYVAL 10

GTimer timerLED(MS);     // Таймер для светодиода
GTimer timerSend(MS);    // Таймер для отпрвки
GTimer timerUpdate(MS);  // Таймер для обновления
GTimer timerInc(MS);     // Таймер для таймера датчика
GTimer timerCsvSave(MS); // Таймер для соханения CSV

// Данные для WiFi
const char *WIFI_SSID = "LABNET";
const char *WIFI_PASSWORD = "h4X7F$ejZi";

const int LED_UPDATE_MSECS = 2;     // Обновлять светодиод каждые N миллисекунд
const int JSON_SIZE = 4096;         // Максимальный размер JSON
const int UPDATE_TIME_SECS = 1;     // Обновлять данные с датчиков каждые N секунд
const int SEND_TIME_SECS = 3;       // Отправлять данные каждые N секунд
const int CSV_SAVE_TIME_SECS = 60;  // Сохранять данные в CSV каждые N секунд
const int SENSOR_ID = 0;            // ID сенсора
const String SENSOR_NAME = "SHT31"; // Имя сенсора

IPAddress server(192, 168, 137, 1); // Адрес MQTT серевера

String dateClear = "";
WiFiClient wclient;
PubSubClient client(wclient, server); // MQQT клиент
Adafruit_SHT31 sht31 = Adafruit_SHT31();

String temperatureSTR = "", humiditySTR = "";
int unixNow = 0, counterLED = 0, stateNow = 1;
bool uppingLED = true;

const String TempSubAdressDelayH = "устройство/SHT31/температура/уставка/delayH";
const String TempSubAdressDelayHH = "устройство/SHT31/температура/уставка/delayHH";
const String TempSubAdressDelayL = "устройство/SHT31/температура/уставка/delayL";
const String TempSubAdressDelayLL = "устройство/SHT31/температура/уставка/delayLL";
const String TempSubAdressH = "устройство/SHT31/температура/уставка/H";
const String TempSubAdressHH = "устройство/SHT31/температура/уставка/HH";
const String TempSubAdressL = "устройство/SHT31/температура/уставка/L";
const String TempSubAdressLL = "устройство/SHT31/температура/уставка/LL";
const String TempSubAdressMask = "устройство/SHT31/температура/уставка/mask";
const String HumSubAdressDelayH = "устройство/SHT31/влажность/уставка/delayH";
const String HumSubAdressDelayHH = "устройство/SHT31/влажность/уставка/delayHH";
const String HumSubAdressDelayL = "устройство/SHT31/влажность/уставка/delayL";
const String HumSubAdressDelayLL = "устройство/SHT31/влажность/уставка/delayLL";
const String HumSubAdressH = "устройство/SHT31/влажность/уставка/H";
const String HumSubAdressHH = "устройство/SHT31/влажность/уставка/HH";
const String HumSubAdressL = "устройство/SHT31/влажность/уставка/L";
const String HumSubAdressLL = "устройство/SHT31/влажность/уставка/LL";
const String HumSubAdressMask = "устройство/SHT31/влажность/уставка/mask";
const String SubAdressArchive = "устройство/SHT31/получитьАрхив";

// Структура датчика
struct Sensor {
  int id, state;
  float PV, HH, H, L, LL, delayHH, delayH, delayL, delayLL;
  int timerHH = -1, timerH = -1, timerL = -1, timerLL = -1;
  String name, param;
  byte flag, mask;
};
Sensor temperature, humidity;
// Файлы в директории
void listDir(const char *dirname) {
  Serial.printf("Listing directory: %s\n", dirname);
  Dir root = LittleFS.openDir(dirname);
  while (root.next()) {
    File file = root.openFile("r");
    Serial.print("  FILE: ");
    Serial.print(root.fileName());
    Serial.print("  SIZE: ");
    Serial.print(file.size());
    time_t cr = file.getCreationTime();
    time_t lw = file.getLastWrite();
    file.close();
    struct tm *tmstruct = localtime(&cr);
    Serial.printf("    CREATION: %d-%02d-%02d %02d:%02d:%02d\n",
                  (tmstruct->tm_year) + 1900, (tmstruct->tm_mon) + 1,
                  tmstruct->tm_mday, tmstruct->tm_hour, tmstruct->tm_min,
                  tmstruct->tm_sec);
    tmstruct = localtime(&lw);
    Serial.printf("  LAST WRITE: %d-%02d-%02d %02d:%02d:%02d\n",
                  (tmstruct->tm_year) + 1900, (tmstruct->tm_mon) + 1,
                  tmstruct->tm_mday, tmstruct->tm_hour, tmstruct->tm_min,
                  tmstruct->tm_sec);
  }
}
// Изменить цвет светодиода
// 1 - зелёный
// 2 - оранжевый
// 3 - красный
void changeColor(int color) {
  pixels.setBrightness(counterLED);
  if (color == 1)
    pixels.setPixelColor(0, pixels.Color(0, 255, 0));
  else if (color == 2)
    pixels.setPixelColor(0, pixels.Color(255, 179, 0));
  else if (color == 3)
    pixels.setPixelColor(0, pixels.Color(255, 0, 0));
  pixels.show();
  if (uppingLED)
    counterLED += 5;
  else
    counterLED -= 5;
  if (counterLED >= 255 or counterLED <= 0)
    uppingLED = !uppingLED;
}
// Прочитать файл
void readFile(const char *path) {
  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }
  Serial.print("Read from file" + String(path) + ": ");
  while (file.available())
    Serial.write(file.read());
  file.close();
}
// Запись в файл
void writeFile(String path, String message) {
  File file = LittleFS.open(path, "a");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (!file.print(message))
    Serial.println("Write failed");
  delay(2000);
  file.close();
}
// Добавление в файл
void appendFile(String path, String message) {
  File file = LittleFS.open(path, "a");
  if (!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  if (!file.print(message))
    Serial.println("Append failed");
  file.close();
}
// Удаление файла
void deleteFile(String path) {
  if (LittleFS.remove(path))
    Serial.println("File deleted");
  else
    Serial.println("Delete failed");
}
// Сохранение уставок температуры
void jsonFileWriteTemp() {
  DynamicJsonDocument data(JSON_SIZE);
  File file = LittleFS.open("/temperature.json", "w");
  data["HH"] = temperature.HH;
  data["H"] = temperature.H;
  data["L"] = temperature.L;
  data["LL"] = temperature.LL;
  data["delayHH"] = temperature.delayHH;
  data["delayH"] = temperature.delayH;
  data["delayL"] = temperature.delayL;
  data["delayLL"] = temperature.delayLL;
  data["timerHH"] = temperature.timerHH;
  data["timerH"] = temperature.timerH;
  data["timerL"] = temperature.timerL;
  data["timerLL"] = temperature.timerLL;
  data["mask"] = temperature.mask;
  data["state"] = temperature.state;
  serializeJson(data, file);
  file.close();
  delay(10);
}
// Чтение уставок температуры
void jsonFileReadTemp() {
  delay(10);
  File file = LittleFS.open("/temperature.json", "r");
  DynamicJsonDocument doc(JSON_SIZE);
  deserializeJson(doc, file);
  temperature.HH = doc["HH"];
  temperature.H = doc["H"];
  temperature.L = doc["L"];
  temperature.LL = doc["LL"];
  temperature.delayHH = doc["delayHH"];
  temperature.delayH = doc["delayH"];
  temperature.delayL = doc["delayL"];
  temperature.delayLL = doc["delayLL"];
  temperature.timerHH = doc["timerHH"];
  temperature.timerH = doc["timerH"];
  temperature.timerL = doc["timerL"];
  temperature.timerLL = doc["timerLL"];
  temperature.mask = doc["mask"];
  file.close();
}
// Сохранение уставок влажности
void jsonFileWriteHum() {
  DynamicJsonDocument data(JSON_SIZE);
  File file = LittleFS.open("/humidity.json", "w");
  data["HH"] = humidity.HH;
  data["H"] = humidity.H;
  data["L"] = humidity.L;
  data["LL"] = humidity.LL;
  data["delayHH"] = humidity.delayHH;
  data["delayH"] = humidity.delayH;
  data["delayL"] = humidity.delayL;
  data["delayLL"] = humidity.delayLL;
  data["timerHH"] = humidity.timerHH;
  data["timerH"] = humidity.timerH;
  data["timerL"] = humidity.timerL;
  data["timerLL"] = humidity.timerLL;
  data["mask"] = humidity.mask;
  serializeJson(data, file);
  file.close();
}
// Чтение уставок влажности
void jsonFileReadHum() {
  File file = LittleFS.open("/humidity.json", "r");
  DynamicJsonDocument doc(JSON_SIZE);
  deserializeJson(doc, file);
  humidity.HH = doc["HH"];
  humidity.H = doc["H"];
  humidity.L = doc["L"];
  humidity.LL = doc["LL"];
  humidity.delayHH = doc["delayHH"];
  humidity.delayH = doc["delayH"];
  humidity.delayL = doc["delayL"];
  humidity.delayLL = doc["delayLL"];
  humidity.timerHH = doc["timerHH"];
  humidity.timerH = doc["timerH"];
  humidity.timerL = doc["timerL"];
  humidity.timerLL = doc["timerLL"];

  humidity.mask = doc["mask"];
  file.close();
}
// Функция, которая отправляет статус при уставке
void notification(String param, String setpoint, String status) {
  DynamicJsonDocument data(JSON_SIZE);
  data["param"] = param;
  data["setpoint"] = setpoint;
  data["status"] = status;
  if (client.connected()) {
    String lineForSend;
    serializeJson(data, lineForSend);
    Serial.println(lineForSend);
    client.publish("устройство/SHT31/уведомление", lineForSend);
  }
}
// Функция, вызываемая при входящем сообщении MQTT
void callback(const MQTT::Publish &pub) {
  // Температура
  if (pub.topic().endsWith("/температура/уставка/delayH")) {
    if ((pub.payload_string()).toInt() > 0) {
      temperature.delayH = (pub.payload_string()).toInt();
      Serial.println(temperature.delayH);
      Serial.println(pub.topic());
      notification("температура", "delayH", "Уставка успешно изменена на " + String((pub.payload_string()).toInt()));
      jsonFileWriteTemp();
    } else
      notification("температура", "delayH", "Уставка не изменена");
    return;
  }
  if (pub.topic().endsWith("/температура/уставка/delayHH")) {
    if ((pub.payload_string()).toInt() > 0) {
      temperature.delayHH = (pub.payload_string()).toInt();
      Serial.println(temperature.delayHH);
      Serial.println(pub.topic());
      notification("температура", "delayHH", "Уставка успешно изменена на " + String((pub.payload_string()).toInt()));
      jsonFileWriteTemp();
    } else
      notification("температура", "delayHH", "Уставка не изменена");
    return;
  }
  if (pub.topic().endsWith("/температура/уставка/delayL")) {
    if ((pub.payload_string()).toInt() > 0) {
      temperature.delayL = (pub.payload_string()).toInt();
      Serial.println(temperature.delayL);
      Serial.println(pub.topic());
      notification("температура", "delayL", "Уставка успешно изменена на " + String((pub.payload_string()).toInt()));
      jsonFileWriteTemp();
    } else
      notification("температура", "delayL", "Уставка не изменена");
    return;
  }
  if (pub.topic().endsWith("/температура/уставка/delayLL")) {
    if ((pub.payload_string()).toInt() > 0) {
      temperature.delayLL = (pub.payload_string()).toInt();
      Serial.println(temperature.delayLL);
      Serial.println(pub.topic());
      notification("температура", "delayLL", "Уставка успешно изменена на " + String((pub.payload_string()).toInt()));
      jsonFileWriteTemp();
    } else
      notification("температура", "delayLL", "Уставка не изменена");
    return;
  }
  if (pub.topic().endsWith("/температура/уставка/H")) {
    temperature.H = (pub.payload_string()).toFloat();
    Serial.println(temperature.H);
    Serial.println(pub.topic());
    notification("температура", "H", "Уставка успешно изменена на " + String((pub.payload_string()).toFloat()));
    jsonFileWriteTemp();
    return;
  }
  if (pub.topic().endsWith("/температура/уставка/HH")) {
    temperature.HH = (pub.payload_string()).toFloat();
    Serial.println(temperature.HH);
    Serial.println(pub.topic());
    notification("температура", "HH", "Уставка успешно изменена на " + String((pub.payload_string()).toFloat()));
    jsonFileWriteTemp();
    return;
  }
  if (pub.topic().endsWith("/температура/уставка/L")) {
    temperature.L = (pub.payload_string()).toFloat();
    Serial.println(temperature.L);
    Serial.println(pub.topic());
    notification("температура", "L", "Уставка успешно изменена на " + String((pub.payload_string()).toFloat()));
    jsonFileWriteTemp();
    return;
  }
  if (pub.topic().endsWith("/температура/уставка/LL")) {
    temperature.LL = (pub.payload_string()).toFloat();
    Serial.println(temperature.LL);
    Serial.println(pub.topic());
    notification("температура", "LL", "Уставка успешно изменена на " + String((pub.payload_string()).toFloat()));
    jsonFileWriteTemp();
    return;
  }
  if (pub.topic().endsWith("/температура/уставка/mask")) {
    temperature.mask = (pub.payload_string()).toInt();
    Serial.println(temperature.mask);
    Serial.println(pub.topic());
    notification("температура", "mask", "Уставка успешно изменена на " + String((pub.payload_string()).toInt()));
    jsonFileWriteTemp();
    return;
  }

  // Влажность
  if (pub.topic().endsWith("/влажность/уставка/delayH")) {
    if ((pub.payload_string()).toInt() > 0) {
      humidity.delayH = (pub.payload_string()).toInt();
      Serial.println(humidity.delayH);
      Serial.println(pub.topic());
      notification("влажность", "delayH", "Уставка успешно изменена на " + String((pub.payload_string()).toInt()));
      jsonFileWriteHum();
    }
    return;
  }
  if (pub.topic().endsWith("/влажность/уставка/delayHH")) {
    if ((pub.payload_string()).toInt() > 0) {
      humidity.delayHH = (pub.payload_string()).toInt();
      Serial.println(humidity.delayHH);
      Serial.println(pub.topic());
      notification("влажность", "delayHH", "Уставка успешно изменена на " + String((pub.payload_string()).toInt()));
      jsonFileWriteHum();
    }
    return;
  }
  if (pub.topic().endsWith("/влажность/уставка/delayL")) {
    if ((pub.payload_string()).toInt() > 0) {
      humidity.delayL = (pub.payload_string()).toInt();
      Serial.println(humidity.delayL);
      Serial.println(pub.topic());
      notification("влажность", "delayL", "Уставка успешно изменена на " + String((pub.payload_string()).toInt()));
      jsonFileWriteHum();
    }
    return;
  }
  if (pub.topic().endsWith("/влажность/уставка/delayLL")) {
    if ((pub.payload_string()).toInt() > 0) {
      humidity.delayLL = (pub.payload_string()).toInt();
      Serial.println(humidity.delayLL);
      Serial.println(pub.topic());
      notification("влажность", "delayLL", "Уставка успешно изменена на " + String((pub.payload_string()).toInt()));
      jsonFileWriteHum();
    }
    return;
  }
  if (pub.topic().endsWith("/влажность/уставка/H")) {
    humidity.H = (pub.payload_string()).toFloat();
    Serial.println(humidity.H);
    Serial.println(pub.topic());
    notification("влажность", "H", "Уставка успешно изменена на " + String((pub.payload_string()).toFloat()));
    jsonFileWriteHum();
    return;
  }
  if (pub.topic().endsWith("/влажность/уставка/HH")) {
    humidity.HH = (pub.payload_string()).toFloat();
    Serial.println(humidity.HH);
    Serial.println(pub.topic());
    notification("влажность", "HH", "Уставка успешно изменена на " + String((pub.payload_string()).toFloat()));
    jsonFileWriteHum();
    return;
  }
  if (pub.topic().endsWith("/влажность/уставка/L")) {
    humidity.L = (pub.payload_string()).toFloat();
    Serial.println(humidity.L);
    Serial.println(pub.topic());
    notification("влажность", "L", "Уставка успешно изменена на " + String((pub.payload_string()).toFloat()));
    jsonFileWriteHum();
    return;
  }
  if (pub.topic().endsWith("/влажность/уставка/LL")) {
    humidity.LL = (pub.payload_string()).toFloat();
    Serial.println(humidity.LL);
    Serial.println(pub.topic());
    notification("влажность", "LL", "Уставка успешно изменена на " + String((pub.payload_string()).toFloat()));
    jsonFileWriteHum();
    return;
  }
  if (pub.topic().endsWith("/влажность/уставка/mask")) {
    humidity.mask = (pub.payload_string()).toInt();
    Serial.println(humidity.mask);
    Serial.println(pub.topic());
    notification("влажность", "mask", "Уставка успешно изменена на " + String((pub.payload_string()).toInt()));
    jsonFileWriteHum();
    return;
  }

  if (pub.topic().endsWith("/SHT31/получитьАрхив")) {
    UnixTime stamp(3);
    stamp.getDateTime(getTime());
    String dd = ((stamp.day < 10) ? ("0" + String(stamp.day)) : (stamp.day));
    String mm =
        ((stamp.month < 10) ? ("0" + String(stamp.month)) : (stamp.month));

    String datenow = "/" + dd + mm + String(stamp.year) + ".csv";
    Serial.println("Запрос на получение архива " + datenow);
    char charBuf[50];
    datenow.toCharArray(charBuf, 50);
    sendArchive(charBuf);
    return;
  }
}
// Генератор JSON из структуры
String json(Sensor sensor) {
  DynamicJsonDocument data(JSON_SIZE);
  data["id"] = sensor.id;
  data["name"] = sensor.name;
  data["param"] = sensor.param;
  data["PV"] = sensor.PV;
  data["HH"] = sensor.HH;
  data["H"] = sensor.H;
  data["L"] = sensor.L;
  data["LL"] = sensor.LL;
  data["delayHH"] = sensor.delayHH;
  data["delayH"] = sensor.delayH;
  data["delayL"] = sensor.delayL;
  data["delayLL"] = sensor.delayLL;
  data["timerHH"] = sensor.timerHH;
  data["timerH"] = sensor.timerH;
  data["timerL"] = sensor.timerL;
  data["timerLL"] = sensor.timerLL;
  data["state"] = sensor.state;
  data["flag"] = sensor.flag;
  data["mask"] = sensor.mask;
  String res;
  serializeJson(data, res);
  return res;
}
int millisSec() { return millis() / 1000; }
// Генератор JSON для строки CSV
String archiveJson(String csv_line) {
  DynamicJsonDocument data(JSON_SIZE);
  int last = 0;
  String ts = "-1", temp = "-1", hum = "-1";
  for (int i = 0; i < csv_line.length(); i++) {
    if (csv_line[i] == ',') {
      if (ts == "-1") {
        ts = csv_line.substring(last, i);
      } else if (temp = "-1") {
        temp = csv_line.substring(last, i);
        last = i + 1;
        break;
      }
      last = i + 1;
    }
  }
  hum = csv_line.substring(last, csv_line.length());
  data["id"] = SENSOR_ID;
  data["ts"] = ts.toInt();
  data["temperature"] = temp.toFloat();
  data["humidity"] = hum.toFloat();
  String res;
  serializeJson(data, res);
  return res;
}
// Отправка архива CSV
void sendArchive(const char *path) {
  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }
  while (file.available()) {
    if (client.connected()) {
      String lineForSend = archiveJson(file.readStringUntil('\n'));
      Serial.println(lineForSend);
      client.publish("устройство/SHT31/архив", lineForSend);
    }
  }
  file.close();
}
/**
 * Состояние датчика
 *
 * 1 – всё нормально
 * 2 – превышено H (HH > PV > H)
 * 3 – превышено HH (PV > HH)
 * 4 – превышено L (LL < PV < L)
 * 5 – превышено LL (PV < LL)
 * 6 – датчик отвалился
 */
int sensorState(Sensor sensor) {
  if (isnan(sensor.PV)) { // sens not working
    return 6;
  }

  if (sensor.timerHH >= sensor.delayHH)
    return 3;
  else if (sensor.timerH >= sensor.delayH)
    return 2;
  else if (sensor.timerL >= sensor.delayL)
    return 4;
  else if (sensor.timerLL >= sensor.delayLL)
    return 5;

  return 1;
}
// Отправка текущих данных
void send() {
  temperatureSTR = json(temperature);
  humiditySTR = json(humidity);

  if (client.connected()) {
    client.publish("устройство/SHT31/температура", temperatureSTR);
    client.publish("устройство/SHT31/влажность", humiditySTR);

    Serial.println("Температура: " + temperatureSTR);
    Serial.println("Влажность: " + humiditySTR);
  }
}
// Установка флага для датчика
void setFlag(Sensor &sensor) {
  sensor.flag = 0;
  bool isok = true;
  if (isnan(sensor.PV) and !bitRead(sensor.mask, 6)) { // sens not working
    bitSet(sensor.flag, 6);
    isok = false;
  }
  if (sensor.timerHH >= sensor.delayHH and !bitRead(sensor.mask, 3)) {
    bitSet(sensor.flag, 3);
    isok = false;
  }
  if (sensor.timerH >= sensor.delayH and !bitRead(sensor.mask, 2)) {
    bitSet(sensor.flag, 2);
    isok = false;
  }
  if (sensor.timerL >= sensor.delayL and !bitRead(sensor.mask, 4)) {
    bitSet(sensor.flag, 4);
    isok = false;
  }
  if (sensor.timerLL >= sensor.delayLL and !bitRead(sensor.mask, 5)) {
    bitSet(sensor.flag, 5);
    isok = false;
  }
  if (isok and !bitRead(sensor.mask, 1))
    bitSet(sensor.flag, 1);
}
// Обновление динамечских данных
void update() {
  temperature.PV = (!isnan(sht31.readTemperature()) ? sht31.readTemperature() - 3.0F : sht31.readTemperature());
  humidity.PV = sht31.readHumidity();
  // Запуск таймеров
  if ((temperature.PV >= temperature.HH + 0.1) && (temperature.timerHH == -1))
    temperature.timerHH = 0;
  if ((temperature.PV >= temperature.H + 0.1) && (temperature.timerH == -1))
    temperature.timerH = 0;
  if ((temperature.PV <= temperature.L - 0.1) && (temperature.timerL == -1))
    temperature.timerH = 0;
  if ((temperature.PV <= temperature.LL - 0.1) && (temperature.timerLL == -1))
    temperature.timerLL = 0;

  // Остановка таймеров
  if (temperature.PV <= temperature.HH - 0.1)
    temperature.timerHH = -1;
  if (temperature.PV <= temperature.H - 0.1)
    temperature.timerH = -1;
  if (temperature.PV >= temperature.L + 0.1)
    temperature.timerL = -1;
  if (temperature.PV >= temperature.LL + 0.1)
    temperature.timerLL = -1;

  // Запуск таймеров
  if ((humidity.PV >= humidity.HH + 1) && (humidity.timerHH == -1))
    humidity.timerHH = 0;
  if ((humidity.PV >= humidity.H + 1) && (humidity.timerH == -1))
    humidity.timerH = 0;
  if ((humidity.PV <= humidity.L - 1) && (humidity.timerL == -1))
    humidity.timerH = 0;
  if ((humidity.PV <= humidity.LL - 1) && (humidity.timerLL == -1))
    humidity.timerLL = 0;

  // Остановка таймеров
  if (humidity.PV <= humidity.HH - 1)
    humidity.timerHH = -1;
  if (humidity.PV <= humidity.H - 1)
    humidity.timerH = -1;
  if (humidity.PV >= humidity.L + 1)
    humidity.timerL = -1;
  if (humidity.PV >= humidity.LL + 1)
    humidity.timerLL = -1;

  temperature.state = sensorState(temperature);
  setFlag(temperature);
  humidity.state = sensorState(humidity);
  setFlag(humidity);
}
// Получение времени с NTP
int getTime() {
  if (unixNow == 0) {
    WiFiUDP ntpUDP;
    NTPClient timeClient(ntpUDP, "pool.ntp.org");
    timeClient.begin();
    timeClient.update();
    unixNow = timeClient.getEpochTime() - millisSec();
  }
  return unixNow + millisSec();
}
// Переводит UNIX-время в нормальный вид
String normalTime(int unix) {
  UnixTime stamp(3);
  stamp.getDateTime(unix);
  String day = ((stamp.day < 10) ? ("0" + String(stamp.day)) : (stamp.day));
  String month = ((stamp.month < 10) ? ("0" + String(stamp.month)) : (stamp.month));
  String hour = ((stamp.hour < 10) ? ("0" + String(stamp.hour)) : (stamp.hour));
  String minute = ((stamp.minute < 10) ? ("0" + String(stamp.minute)) : (stamp.minute));
  String second = ((stamp.second < 10) ? ("0" + String(stamp.second)) : (stamp.second));
  return day + "." + month + "." + String(stamp.year) + " " + hour + ":" + minute + ":" + second;
}
// Сохранение архива CSV
void csvSaver() {
  UnixTime stamp(3);
  stamp.getDateTime(getTime());
  String dd = ((stamp.day < 10) ? ("0" + String(stamp.day)) : (stamp.day));
  String mm = ((stamp.month < 10) ? ("0" + String(stamp.month)) : (stamp.month));
  String datenow = "/" + dd + mm + String(stamp.year) + ".csv";
  if ((dd + mm + String(stamp.year)) != dateClear) {
    dateClear = dd + mm + String(stamp.year);
    String datenow = "/" + dd + mm + String(stamp.year) + ".csv"; // если день не совпадает, создается новый csv
    writeFile(datenow, "");                                       // используется для "перехода в след. день"
  }
  String tsLog = String(getTime()) + "," + String(temperature.PV) + "," + String(humidity.PV) + "\n";
  appendFile(datenow, tsLog); // выполняется раз в n секунд/дней/часов
  stamp.getDateTime(getTime() - 4 * 86400);
  if (String((stamp.day)).length() == 1)
    dd = ("0" + String(stamp.day));
  else
    dd = String(stamp.day);
  if (String((stamp.month)).length() == 1)
    mm = ("0" + String(stamp.month));
  else
    mm = String(stamp.month);
  deleteFile("/" + dd + mm + String(stamp.year) + ".csv");
  stamp.getDateTime(getTime() - 5 * 86400);
  if (String((stamp.day)).length() == 1)
    dd = ("0" + String(stamp.day));
  else
    dd = String(stamp.day);
  if (String((stamp.month)).length() == 1)
    mm = ("0" + String(stamp.month));
  else
    mm = String(stamp.month);
  deleteFile("/" + dd + mm + String(stamp.year) + ".csv");
  stamp.getDateTime(getTime() - 6 * 86400);
  if (String((stamp.day)).length() == 1)
    dd = ("0" + String(stamp.day));
  else
    dd = String(stamp.day);
  if (String((stamp.month)).length() == 1)
    mm = ("0" + String(stamp.month));
  else
    mm = String(stamp.month);
  // удаление файлов за последние 3 дня
  deleteFile("/" + dd + mm + String(stamp.year) + ".csv");
  stamp.getDateTime(getTime());

  delay(1000);
}
void connectMQQT() {
  if (client.connect(MQTT::Connect("SHT31_" + String(ESP.getChipId())).set_auth("Sensors", "h4X7F$ejZi"))) {
    client.set_callback(callback);
    delay(10);
    client.subscribe(TempSubAdressDelayH);
    client.subscribe(TempSubAdressDelayHH);
    client.subscribe(TempSubAdressDelayL);
    client.subscribe(TempSubAdressDelayLL);
    client.subscribe(TempSubAdressH);
    client.subscribe(TempSubAdressHH);
    client.subscribe(TempSubAdressL);
    client.subscribe(TempSubAdressLL);
    client.subscribe(TempSubAdressMask);
    client.subscribe(HumSubAdressDelayH);
    client.subscribe(HumSubAdressDelayHH);
    client.subscribe(HumSubAdressDelayL);
    client.subscribe(HumSubAdressDelayLL);
    client.subscribe(HumSubAdressH);
    client.subscribe(HumSubAdressHH);
    client.subscribe(HumSubAdressL);
    client.subscribe(HumSubAdressLL);
    client.subscribe(HumSubAdressMask);
    client.subscribe(SubAdressArchive);
  } else {
    if (!timerCsvSave.isEnabled()) {
      Serial.println("Соединение разорвано");
      timerCsvSave.start();
    }
  }
}
void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println();
  WiFi.mode(WIFI_STA);

  String infoID = String(ESP.getChipId());
  String infoRSSI = String(WiFi.RSSI());
  String infoI2C = "true";

  if (!sht31.begin(0x45)) {
    Serial.println("SHT31 не найден");
    infoI2C = "false";
  }
  String infoFS = "true";
  if (!LittleFS.begin()) {
    Serial.println("Монитирование LittleFS не удалось");
    infoFS = "false";
  }
  String infoIP, infoMAC;
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Подключение к ");
    Serial.print(WIFI_SSID);
    Serial.println("...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    if (WiFi.waitForConnectResult() != WL_CONNECTED)
      return;
    infoIP = WiFi.localIP().toString();
    infoMAC = String(WiFi.macAddress());
  }

  String info = "{\"id\":" + infoID + ",\"name\":\"" + SENSOR_NAME +
                "\",\"params\":[\"температура\",\"влажность\"],"
                "\"RSSI\":" +
                infoRSSI + ",\"FS\":" + infoFS + ",\"I2C\":" + infoI2C +
                ",\"IP\":\"" + infoIP + "\",\"MAC\":\"" + infoMAC + "\"}";

  connectMQQT();

  if (client.connected()) {
    client.publish("устройство/SHT31/конфиг", info);
    Serial.println(info);
  }

#if defined(__AVR_ATtiny85__) && (F_CPU == 16000000)
  clock_prescale_set(clock_div_1);
#endif
  pixels.begin();

  // Запуск таймеров
  timerLED.setInterval(LED_UPDATE_MSECS);
  timerSend.setInterval(SEND_TIME_SECS * 1000);
  timerUpdate.setInterval(UPDATE_TIME_SECS * 1000);
  timerInc.setInterval(1000);
  timerCsvSave.setInterval(CSV_SAVE_TIME_SECS * 1000);
  timerLED.start();
  timerSend.start();
  timerUpdate.start();

  // Установка статических параметров
  temperature.id = SENSOR_ID;
  temperature.name = SENSOR_NAME;
  temperature.param = "температура";
  humidity.id = SENSOR_ID;
  humidity.name = SENSOR_NAME;
  humidity.param = "влажность";

  jsonFileReadTemp();
  jsonFileReadHum();

  Serial.println("Сейчас " + normalTime(getTime()));
  Serial.println();
}
void loop() {
  // Готовность таймеров
  if (timerUpdate.isReady())
    update();
  if (timerSend.isReady()) {
    send();
    jsonFileWriteTemp();
    jsonFileWriteHum();
  }
  if (timerCsvSave.isReady())
    csvSaver();

  if (timerLED.isReady()) {
    if (temperature.state == 3 or temperature.state == 5 or temperature.state == 6 or humidity.state == 3 or humidity.state == 5 or humidity.state == 6)
      changeColor(3);
    else if (temperature.state == 2 or temperature.state == 4 or humidity.state == 2 or humidity.state == 4)
      changeColor(2);
    else
      changeColor(1);
  }

  // Увеличение таймеров датчиков
  if (timerInc.isReady()) {
    if (temperature.timerH != -1)
      temperature.timerH++;
    if (temperature.timerHH != -1)
      temperature.timerHH++;
    if (temperature.timerL != -1)
      temperature.timerL++;
    temperature.timerL++;
    if (temperature.timerLL != -1)
      temperature.timerLL++;
    if (humidity.timerH != -1)
      humidity.timerH++;
    if (humidity.timerHH != -1)
      humidity.timerHH++;
    if (humidity.timerL != -1)
      humidity.timerL++;
    if (humidity.timerLL != -1)
      humidity.timerLL++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    pixels.setBrightness(255);
    pixels.setPixelColor(0, pixels.Color(255, 0, 0));
    pixels.show();
    
    Serial.print("Подключение к ");
    Serial.print(WIFI_SSID);
    Serial.println("...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
      if (!timerCsvSave.isEnabled()) {
        Serial.println("Соединение с WiFi разорвано");
        timerCsvSave.start();
      }
      return;
    }
    Serial.println("WiFi подключен");
  }
  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected())
      connectMQQT();
    if (client.connected())
      client.loop();
    delay(10);
  }
  if (WiFi.status() == WL_CONNECTED and client.connected()) {
    if (timerCsvSave.isEnabled()) {
      Serial.println("Соединение восстановлено");
      timerCsvSave.stop();
    }
  }
}
