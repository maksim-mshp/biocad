#include <Adafruit_BME280.h>
#include <Adafruit_Sensor.h>
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
#include <Wire.h>

GTimer timerSend(MS);    // Таймер для отпрвки
GTimer timerUpdate(MS);  // Таймер для обновления
GTimer timerInc(MS);     // Таймер для таймера датчика
GTimer timerCsvSave(MS); // Таймер для соханения CSV

// Данные для WiFi
const char *WIFI_SSID = "LABNET";
const char *WIFI_PASSWORD = "h4X7F$ejZi";

const int JSON_SIZE = 4096;          // Максимальный размер JSON
const int UPDATE_TIME_SECS = 1;      // Обновлять данные с датчиков каждые N секунд
const int SEND_TIME_SECS = 3;        // Отправлять данные каждые N секунд
const int CSV_SAVE_TIME_SECS = 60;   // Сохранять данные в CSV каждые N секунд
const int SENSOR_ID = 2;             // ID сенсора
const String SENSOR_NAME = "BME280"; // Имя сенсора

IPAddress server(192, 168, 137, 1); // Адрес MQTT серевера

String dateClear = "";
WiFiClient wclient;
PubSubClient client(wclient, server); // MQQT клиент

#define BME280_PIN D6
#define SEALEVELPRESSURE_HPA (5) // Задаем высоту
Adafruit_BME280 bme;

String jsonRes = "";
int unixNow = 0, resetConter = 0;
int duration, starttime, lowpulseoccupancy = 0;
float ratio = 0, pressrat = 0;

const String SubAdressDelayH = "устройство/BME280/давление/уставка/delayH";
const String SubAdressDelayHH = "устройство/BME280/давление/уставка/delayHH";
const String SubAdressDelayL = "устройство/BME280/давление/уставка/delayL";
const String SubAdressDelayLL = "устройство/BME280/давление/уставка/delayLL";
const String SubAdressH = "устройство/BME280/давление/уставка/H";
const String SubAdressHH = "устройство/BME280/давление/уставка/HH";
const String SubAdressL = "устройство/BME280/давление/уставка/L";
const String SubAdressLL = "устройство/BME280/давление/уставка/LL";
const String SubAdressMask = "устройство/BME280/давление/уставка/mask";
const String SubAdressArchive = "устройство/BME280/получитьАрхив";
// Структура датчика
struct Sensor {
  int id, state;
  float PV, HH, H, L, LL, delayHH, delayH, delayL, delayLL;
  int timerHH = -1, timerH = -1, timerL = -1, timerLL = -1;
  String name, param;
  byte flag, mask;
};
Sensor pressure;
// Получить давление
float getCO2() {
  return bme.readPressure() / 100.0F * 0.000987F;
}
bool isInvalid(float n) {
  return (isnan(n) or (n >= 1.22 and n <= 1.24) or n == 0);
}
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
// Сохранение уставок давления
void jsonFileWrite() {
  DynamicJsonDocument data(JSON_SIZE);
  File file = LittleFS.open("/pressure.json", "w");
  data["HH"] = pressure.HH;
  data["H"] = pressure.H;
  data["L"] = pressure.L;
  data["LL"] = pressure.LL;
  data["delayHH"] = pressure.delayHH;
  data["delayH"] = pressure.delayH;
  data["delayL"] = pressure.delayL;
  data["delayLL"] = pressure.delayLL;
  data["timerHH"] = pressure.timerHH;
  data["timerH"] = pressure.timerH;
  data["timerL"] = pressure.timerL;
  data["timerLL"] = pressure.timerLL;
  data["mask"] = pressure.mask;
  data["state"] = pressure.state;
  serializeJson(data, file);
  file.close();
  delay(10);
}
// Чтение уставок давления
void jsonFileRead() {
  delay(10);
  File file = LittleFS.open("/pressure.json", "r");
  DynamicJsonDocument doc(JSON_SIZE);
  deserializeJson(doc, file);
  pressure.HH = doc["HH"];
  pressure.H = doc["H"];
  pressure.L = doc["L"];
  pressure.LL = doc["LL"];
  pressure.delayHH = doc["delayHH"];
  pressure.delayH = doc["delayH"];
  pressure.delayL = doc["delayL"];
  pressure.delayLL = doc["delayLL"];
  pressure.timerHH = doc["timerHH"];
  pressure.timerH = doc["timerH"];
  pressure.timerL = doc["timerL"];
  pressure.timerLL = doc["timerLL"];
  pressure.mask = doc["mask"];
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
    client.publish("устройство/BME280/уведомление", lineForSend);
  }
}
// Функция, вызываемая при входящем сообщении MQTT
void callback(const MQTT::Publish &pub) {
  // Температура
  if (pub.topic().endsWith("/давление/уставка/delayH")) {
    if ((pub.payload_string()).toInt() > 0) {
      pressure.delayH = (pub.payload_string()).toInt();
      Serial.println(pressure.delayH);
      Serial.println(pub.topic());
      notification("давление", "delayH", "Уставка успешно изменена на " + String((pub.payload_string()).toInt()));
      jsonFileWrite();
    } else
      notification("давление", "delayH", "Уставка не изменена");
    return;
  }
  if (pub.topic().endsWith("/давление/уставка/delayHH")) {
    if ((pub.payload_string()).toInt() > 0) {
      pressure.delayHH = (pub.payload_string()).toInt();
      Serial.println(pressure.delayHH);
      Serial.println(pub.topic());
      notification("давление", "delayHH", "Уставка успешно изменена на " + String((pub.payload_string()).toInt()));
      jsonFileWrite();
    } else
      notification("давление", "delayHH", "Уставка не изменена");
    return;
  }
  if (pub.topic().endsWith("/давление/уставка/delayL")) {
    if ((pub.payload_string()).toInt() > 0) {
      pressure.delayL = (pub.payload_string()).toInt();
      Serial.println(pressure.delayL);
      Serial.println(pub.topic());
      notification("давление", "delayL", "Уставка успешно изменена на " + String((pub.payload_string()).toInt()));
      jsonFileWrite();
    } else
      notification("давление", "delayL", "Уставка не изменена");
    return;
  }
  if (pub.topic().endsWith("/давление/уставка/delayLL")) {
    if ((pub.payload_string()).toInt() > 0) {
      pressure.delayLL = (pub.payload_string()).toInt();
      Serial.println(pressure.delayLL);
      Serial.println(pub.topic());
      notification("давление", "delayLL", "Уставка успешно изменена на " + String((pub.payload_string()).toInt()));
      jsonFileWrite();
    } else
      notification("давление", "delayLL", "Уставка не изменена");
    return;
  }
  if (pub.topic().endsWith("/давление/уставка/H")) {
    pressure.H = (pub.payload_string()).toFloat();
    Serial.println(pressure.H);
    Serial.println(pub.topic());
    notification("давление", "H", "Уставка успешно изменена на " + String((pub.payload_string()).toFloat()));
    jsonFileWrite();
    return;
  }
  if (pub.topic().endsWith("/давление/уставка/HH")) {
    pressure.HH = (pub.payload_string()).toFloat();
    Serial.println(pressure.HH);
    Serial.println(pub.topic());
    notification("давление", "HH", "Уставка успешно изменена на " + String((pub.payload_string()).toFloat()));
    jsonFileWrite();
    return;
  }
  if (pub.topic().endsWith("/давление/уставка/L")) {
    pressure.L = (pub.payload_string()).toFloat();
    Serial.println(pressure.L);
    Serial.println(pub.topic());
    notification("давление", "L", "Уставка успешно изменена на " + String((pub.payload_string()).toFloat()));
    jsonFileWrite();
    return;
  }
  if (pub.topic().endsWith("/давление/уставка/LL")) {
    pressure.LL = (pub.payload_string()).toFloat();
    Serial.println(pressure.LL);
    Serial.println(pub.topic());
    notification("давление", "LL", "Уставка успешно изменена на " + String((pub.payload_string()).toFloat()));
    jsonFileWrite();
    return;
  }
  if (pub.topic().endsWith("/давление/уставка/mask")) {
    pressure.mask = (pub.payload_string()).toInt();
    Serial.println(pressure.mask);
    Serial.println(pub.topic());
    notification("давление", "mask", "Уставка успешно изменена на " + String((pub.payload_string()).toInt()));
    jsonFileWrite();
    return;
  }

  if (pub.topic().endsWith("/BME280/получитьАрхив")) {
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
  String ts = "-1", press = "-1";
  for (int i = 0; i < csv_line.length(); i++) {
    if (csv_line[i] == ',') {
      ts = csv_line.substring(last, i);
      last = i + 1;
    }
  }
  press = csv_line.substring(last, csv_line.length());
  data["id"] = SENSOR_ID;
  data["ts"] = ts.toInt();
  data["pressure"] = press.toFloat() / 10000.0F;
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
      client.publish("устройство/BME280/архив", lineForSend);
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
  Serial.println(sensor.PV);
  if (isInvalid(sensor.PV)) { // sens not working
    resetConter++;
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
  jsonRes = json(pressure);

  if (client.connected()) {
    client.publish("устройство/BME280/давление", jsonRes);
    Serial.println("Давление: " + jsonRes);
  }
}
// Установка флага для датчика
void setFlag(Sensor &sensor) {
  sensor.flag = 0;
  bool isok = true;
  if (isInvalid(sensor.PV) and !bitRead(sensor.mask, 6)) { // sens not working
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
  pressure.PV = getCO2();
  // Запуск таймеров
  if ((pressure.PV >= pressure.HH + 0.01) && (pressure.timerHH == -1))
    pressure.timerHH = 0;
  if ((pressure.PV >= pressure.H + 0.01) && (pressure.timerH == -1))
    pressure.timerH = 0;
  if ((pressure.PV <= pressure.L - 0.01) && (pressure.timerL == -1))
    pressure.timerH = 0;
  if ((pressure.PV <= pressure.LL - 0.01) && (pressure.timerLL == -1))
    pressure.timerLL = 0;

  // Остановка таймеров
  if (pressure.PV <= pressure.HH - 0.01)
    pressure.timerHH = -1;
  if (pressure.PV <= pressure.H - 0.01)
    pressure.timerH = -1;
  if (pressure.PV >= pressure.L + 0.01)
    pressure.timerL = -1;
  if (pressure.PV >= pressure.LL + 0.01)
    pressure.timerLL = -1;

  pressure.state = sensorState(pressure);
  setFlag(pressure);
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
  String tsLog = String(getTime()) + "," + String(pressure.PV * 10000) + "\n";
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
  if (client.connect(MQTT::Connect(SENSOR_NAME + "_" + String(ESP.getChipId())).set_auth("Sensors", "h4X7F$ejZi"))) {
    client.set_callback(callback);
    delay(10);
    client.subscribe(SubAdressDelayH);
    client.subscribe(SubAdressDelayHH);
    client.subscribe(SubAdressDelayL);
    client.subscribe(SubAdressDelayLL);
    client.subscribe(SubAdressH);
    client.subscribe(SubAdressHH);
    client.subscribe(SubAdressL);
    client.subscribe(SubAdressLL);
    client.subscribe(SubAdressMask);
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

  if (!bme.begin(0x76)) {
    Serial.println("BME280 не найден");
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
                "\",\"params\":[\"давление\"],"
                "\"RSSI\":" +
                infoRSSI + ",\"FS\":" + infoFS + ",\"I2C\":" + infoI2C +
                ",\"IP\":\"" + infoIP + "\",\"MAC\":\"" + infoMAC + "\"}";

  connectMQQT();

  if (client.connected()) {
    client.publish("устройство/BME280/конфиг", info);
    Serial.println(info);
  }

  // Запуск таймеров
  timerSend.setInterval(SEND_TIME_SECS * 1000);
  timerUpdate.setInterval(UPDATE_TIME_SECS * 1000);
  timerInc.setInterval(1000);
  timerCsvSave.setInterval(CSV_SAVE_TIME_SECS * 1000);
  timerSend.start();
  timerUpdate.start();

  // Установка статических параметров
  pressure.id = SENSOR_ID;
  pressure.name = SENSOR_NAME;
  pressure.param = "давление";

  jsonFileRead();

  Serial.println("Сейчас " + normalTime(getTime()));
}
void loop() {
  // Готовность таймеров
  if (timerUpdate.isReady()) {
    if (resetConter == 5) {
      Serial.println("Reset..");
      ESP.restart();
    }
    update();
  }
  if (timerSend.isReady()) {
    send();
    jsonFileWrite();
  }
  if (timerCsvSave.isReady())
    csvSaver();

  duration = pulseIn(BME280_PIN, LOW);
  lowpulseoccupancy = lowpulseoccupancy + duration;

  // Увеличение таймеров датчиков
  if (timerInc.isReady()) {
    if (pressure.timerH != -1)
      pressure.timerH++;
    if (pressure.timerHH != -1)
      pressure.timerHH++;
    if (pressure.timerL != -1)
      pressure.timerL++;
    pressure.timerL++;
    if (pressure.timerLL != -1)
      pressure.timerLL++;
  }

  if (WiFi.status() != WL_CONNECTED) {
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
