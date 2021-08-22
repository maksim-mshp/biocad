#include "kSeries.h" //include kSeries Library
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

kSeries K_30(D2, D1);

GTimer timerSend(MS);    // Таймер для отпрвки
GTimer timerUpdate(MS);  // Таймер для обновления
GTimer timerInc(MS);     // Таймер для таймера датчика
GTimer timerCsvSave(MS); // Таймер для соханения CSV

// Данные для WiFi
const char *WIFI_SSID = "LABNET";
const char *WIFI_PASSWORD = "h4X7F$ejZi";

const int JSON_SIZE = 4096;         // Максимальный размер JSON
const int UPDATE_TIME_SECS = 1;     // Обновлять данные с датчиков каждые N секунд
const int SEND_TIME_SECS = 3;       // Отправлять данные каждые N секунд
const int CSV_SAVE_TIME_SECS = 60;  // Сохранять данные в CSV каждые N секунд
const int SENSOR_ID = 3;            // ID сенсора
const String SENSOR_NAME = "BLG33"; // Имя сенсора

IPAddress server(192, 168, 137, 1); // Адрес MQTT серевера

String dateClear = "";
WiFiClient wclient;
PubSubClient client(wclient, server); // MQQT клиент

String jsonRes = "";
int unixNow = 0, resetConter = 0;
int duration, starttime, lowpulseoccupancy = 0;
float ratio = 0, pressrat = 0;

const String SubAdressDelayH = "устройство/BLG33/CO2/уставка/delayH";
const String SubAdressDelayHH = "устройство/BLG33/CO2/уставка/delayHH";
const String SubAdressDelayL = "устройство/BLG33/CO2/уставка/delayL";
const String SubAdressDelayLL = "устройство/BLG33/CO2/уставка/delayLL";
const String SubAdressH = "устройство/BLG33/CO2/уставка/H";
const String SubAdressHH = "устройство/BLG33/CO2/уставка/HH";
const String SubAdressL = "устройство/BLG33/CO2/уставка/L";
const String SubAdressLL = "устройство/BLG33/CO2/уставка/LL";
const String SubAdressMask = "устройство/BLG33/CO2/уставка/mask";
const String SubAdressArchive = "устройство/BLG33/получитьАрхив";
// Структура датчика
struct Sensor {
  int id, state;
  float PV, HH, H, L, LL, delayHH, delayH, delayL, delayLL;
  int timerHH = -1, timerH = -1, timerL = -1, timerLL = -1;
  String name, param;
  byte flag, mask;
};
Sensor CO2;
// Получить CO2
float getCO2() {
  return K_30.getCO2('%');
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
  File file = LittleFS.open("/CO2.json", "w");
  data["HH"] = CO2.HH;
  data["H"] = CO2.H;
  data["L"] = CO2.L;
  data["LL"] = CO2.LL;
  data["delayHH"] = CO2.delayHH;
  data["delayH"] = CO2.delayH;
  data["delayL"] = CO2.delayL;
  data["delayLL"] = CO2.delayLL;
  data["timerHH"] = CO2.timerHH;
  data["timerH"] = CO2.timerH;
  data["timerL"] = CO2.timerL;
  data["timerLL"] = CO2.timerLL;
  data["mask"] = CO2.mask;
  data["state"] = CO2.state;
  serializeJson(data, file);
  file.close();
  delay(10);
}
// Чтение уставок давления
void jsonFileRead() {
  delay(10);
  File file = LittleFS.open("/CO2.json", "r");
  DynamicJsonDocument doc(JSON_SIZE);
  deserializeJson(doc, file);
  CO2.HH = doc["HH"];
  CO2.H = doc["H"];
  CO2.L = doc["L"];
  CO2.LL = doc["LL"];
  CO2.delayHH = doc["delayHH"];
  CO2.delayH = doc["delayH"];
  CO2.delayL = doc["delayL"];
  CO2.delayLL = doc["delayLL"];
  CO2.timerHH = doc["timerHH"];
  CO2.timerH = doc["timerH"];
  CO2.timerL = doc["timerL"];
  CO2.timerLL = doc["timerLL"];
  CO2.mask = doc["mask"];
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
    client.publish("устройство/BLG33/уведомление", lineForSend);
  }
}
// Функция, вызываемая при входящем сообщении MQTT
void callback(const MQTT::Publish &pub) {
  // Температура
  if (pub.topic().endsWith("/CO2/уставка/delayH")) {
    if ((pub.payload_string()).toInt() > 0) {
      CO2.delayH = (pub.payload_string()).toInt();
      Serial.println(CO2.delayH);
      Serial.println(pub.topic());
      notification("CO2", "delayH", "Уставка успешно изменена на " + String((pub.payload_string()).toInt()));
      jsonFileWrite();
    } else
      notification("CO2", "delayH", "Уставка не изменена");
    return;
  }
  if (pub.topic().endsWith("/CO2/уставка/delayHH")) {
    if ((pub.payload_string()).toInt() > 0) {
      CO2.delayHH = (pub.payload_string()).toInt();
      Serial.println(CO2.delayHH);
      Serial.println(pub.topic());
      notification("CO2", "delayHH", "Уставка успешно изменена на " + String((pub.payload_string()).toInt()));
      jsonFileWrite();
    } else
      notification("CO2", "delayHH", "Уставка не изменена");
    return;
  }
  if (pub.topic().endsWith("/CO2/уставка/delayL")) {
    if ((pub.payload_string()).toInt() > 0) {
      CO2.delayL = (pub.payload_string()).toInt();
      Serial.println(CO2.delayL);
      Serial.println(pub.topic());
      notification("CO2", "delayL", "Уставка успешно изменена на " + String((pub.payload_string()).toInt()));
      jsonFileWrite();
    } else
      notification("CO2", "delayL", "Уставка не изменена");
    return;
  }
  if (pub.topic().endsWith("/CO2/уставка/delayLL")) {
    if ((pub.payload_string()).toInt() > 0) {
      CO2.delayLL = (pub.payload_string()).toInt();
      Serial.println(CO2.delayLL);
      Serial.println(pub.topic());
      notification("CO2", "delayLL", "Уставка успешно изменена на " + String((pub.payload_string()).toInt()));
      jsonFileWrite();
    } else
      notification("CO2", "delayLL", "Уставка не изменена");
    return;
  }
  if (pub.topic().endsWith("/CO2/уставка/H")) {
    CO2.H = (pub.payload_string()).toFloat();
    Serial.println(CO2.H);
    Serial.println(pub.topic());
    notification("CO2", "H", "Уставка успешно изменена на " + String((pub.payload_string()).toFloat()));
    jsonFileWrite();
    return;
  }
  if (pub.topic().endsWith("/CO2/уставка/HH")) {
    CO2.HH = (pub.payload_string()).toFloat();
    Serial.println(CO2.HH);
    Serial.println(pub.topic());
    notification("CO2", "HH", "Уставка успешно изменена на " + String((pub.payload_string()).toFloat()));
    jsonFileWrite();
    return;
  }
  if (pub.topic().endsWith("/CO2/уставка/L")) {
    CO2.L = (pub.payload_string()).toFloat();
    Serial.println(CO2.L);
    Serial.println(pub.topic());
    notification("CO2", "L", "Уставка успешно изменена на " + String((pub.payload_string()).toFloat()));
    jsonFileWrite();
    return;
  }
  if (pub.topic().endsWith("/CO2/уставка/LL")) {
    CO2.LL = (pub.payload_string()).toFloat();
    Serial.println(CO2.LL);
    Serial.println(pub.topic());
    notification("CO2", "LL", "Уставка успешно изменена на " + String((pub.payload_string()).toFloat()));
    jsonFileWrite();
    return;
  }
  if (pub.topic().endsWith("/CO2/уставка/mask")) {
    CO2.mask = (pub.payload_string()).toInt();
    Serial.println(CO2.mask);
    Serial.println(pub.topic());
    notification("CO2", "mask", "Уставка успешно изменена на " + String((pub.payload_string()).toInt()));
    jsonFileWrite();
    return;
  }

  if (pub.topic().endsWith("/BLG33/получитьАрхив")) {
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
  String ts = "-1", co2 = "-1";
  for (int i = 0; i < csv_line.length(); i++) {
    if (csv_line[i] == ',') {
      ts = csv_line.substring(last, i);
      last = i + 1;
    }
  }
  co2 = csv_line.substring(last, csv_line.length());
  data["id"] = SENSOR_ID;
  data["ts"] = ts.toInt();
  data["CO2"] = co2.toFloat() / 10000.0F;
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
      client.publish("устройство/BLG33/архив", lineForSend);
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
  jsonRes = json(CO2);

  if (client.connected()) {
    client.publish("устройство/BLG33/CO2", jsonRes);
    Serial.println("CO2: " + jsonRes);
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
  CO2.PV = getCO2();
  // Запуск таймеров
  if ((CO2.PV >= CO2.HH + 0.01) && (CO2.timerHH == -1))
    CO2.timerHH = 0;
  if ((CO2.PV >= CO2.H + 0.01) && (CO2.timerH == -1))
    CO2.timerH = 0;
  if ((CO2.PV <= CO2.L - 0.01) && (CO2.timerL == -1))
    CO2.timerH = 0;
  if ((CO2.PV <= CO2.LL - 0.01) && (CO2.timerLL == -1))
    CO2.timerLL = 0;

  // Остановка таймеров
  if (CO2.PV <= CO2.HH - 0.01)
    CO2.timerHH = -1;
  if (CO2.PV <= CO2.H - 0.01)
    CO2.timerH = -1;
  if (CO2.PV >= CO2.L + 0.01)
    CO2.timerL = -1;
  if (CO2.PV >= CO2.LL + 0.01)
    CO2.timerLL = -1;

  CO2.state = sensorState(CO2);
  setFlag(CO2);
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
  String tsLog = String(getTime()) + "," + String(CO2.PV * 10000) + "\n";
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

  // if (!bme.begin(0x76)) {
  //   Serial.println("BLG33 не найден");
  //   infoI2C = "false";
  // }
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
                "\",\"params\":[\"CO2\"],"
                "\"RSSI\":" +
                infoRSSI + ",\"FS\":" + infoFS + ",\"I2C\":" + infoI2C +
                ",\"IP\":\"" + infoIP + "\",\"MAC\":\"" + infoMAC + "\"}";

  connectMQQT();

  if (client.connected()) {
    client.publish("устройство/BLG33/конфиг", info);
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
  CO2.id = SENSOR_ID;
  CO2.name = SENSOR_NAME;
  CO2.param = "CO2";

  jsonFileRead();

  Serial.println("Сейчас " + normalTime(getTime()));
  Serial.println();
  Serial.println();
  Serial.println();
  readFile("/23072021.csv");
  Serial.println();
  Serial.println();
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

  // Увеличение таймеров датчиков
  if (timerInc.isReady()) {
    if (CO2.timerH != -1)
      CO2.timerH++;
    if (CO2.timerHH != -1)
      CO2.timerHH++;
    if (CO2.timerL != -1)
      CO2.timerL++;
    CO2.timerL++;
    if (CO2.timerLL != -1)
      CO2.timerLL++;
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
