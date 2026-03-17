
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ArduinoHA.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>
#include <ElegantOTA.h>
#include <Wire.h>

// ====== Nastavení ======
const unsigned long INTERVAL = 5000;  // 5s interval publikace do MQTT
#define SSID         "****"
#define PASSWORD     "****"
#define BROKER_ADDR IPAddress(192,168,0,104)

// Piny
#define Cirk_PIN    32
#define Hladina_PIN 26
#define DHTPIN      27
#define TlacPin     25
#define DHTTYPE     DHT21
#define ONE_WIRE_BUS 4  // DS18B20 na GPIO4

// Display
#define SCREEN_WIDTH 128 
#define SCREEN_HEIGHT 64 
#define OLED_RESET    -1 

// Debounce pro tlačítko
const unsigned long DEBOUNCE_MS = 50;

// ====== Globální proměnné ======
int stavc = 0;                     // stav cirkulace (0/1)
int numberOfDevices = 0;           // počet 1-Wire senzorů
unsigned long currentMillis = 0;
unsigned long previousPublishMillis = 0;

// Výkony (W)
unsigned long P0 = 0;
unsigned long P1 = 0;
unsigned long P2 = 0;
unsigned long P3 = 0;
unsigned long P4 = 0;
unsigned long P5 = 0;
unsigned long P6 = 0;
unsigned long P7 = 0;
unsigned long P8 = 0;

// ---- DHT průměrování (1 minuta) ----
float h = NAN;          // poslední okamžitý RH z DHT (nepublikuje se přímo)
float t = NAN;          // poslední okamžitá T z DHT (nepublikuje se přímo)
double dhtSumT = 0.0;
double dhtSumH = 0.0;
uint16_t dhtCount = 0;
unsigned long dhtWindowStartMillis = 0;
bool dhtNewMinuteReady = false;  // když je true, publikujeme průměr
float dhtAvgT = NAN;
float dhtAvgH = NAN;

// Displej čas „probuzení“
volatile unsigned long Displej = 0;
volatile unsigned long lastInterruptTime = 0;  // pro debounce ISR

// Wi-Fi reconnect bez delay
unsigned long lastWiFiReconnectAttempt = 0;
const unsigned long WIFI_RECONNECT_INTERVAL_MS = 5000;

// ====== Objekty knihoven ======
WiFiClient client;
WebServer server(80);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
HADevice device("ESP32");
HAMqtt mqtt(client, device);
DHT dht(DHTPIN, DHTTYPE);

// Home Assistant entity
HASensorNumber vykon0("Vykon0", HASensorNumber::PrecisionP0);
HASensorNumber vykon1("Vykon1", HASensorNumber::PrecisionP0);
HASensorNumber vykon2("Vykon2", HASensorNumber::PrecisionP0);
HASensorNumber vykon3("Vykon3", HASensorNumber::PrecisionP0);
HASensorNumber vykon4("Vykon4", HASensorNumber::PrecisionP0);
HASensorNumber vykon5("Vykon5", HASensorNumber::PrecisionP0);
HASensorNumber vykon6("Vykon6", HASensorNumber::PrecisionP0);
HASensorNumber vykon7("Vykon7", HASensorNumber::PrecisionP0);
HASensorNumber vykon8("Vykon8", HASensorNumber::PrecisionP0);

HASensorNumber teplota1("Teplota1", HASensorNumber::PrecisionP1);
HASensorNumber teplota2("Teplota2", HASensorNumber::PrecisionP1);
HASensorNumber teplota3("Teplota3", HASensorNumber::PrecisionP1);
HASensorNumber teplota4("Teplota4", HASensorNumber::PrecisionP1);
HASensorNumber teplota5("Teplota5", HASensorNumber::PrecisionP1);
HASensorNumber teplota6("Teplota6", HASensorNumber::PrecisionP1);
HASensorNumber teplota7("Teplota7", HASensorNumber::PrecisionP1);
HASensorNumber teplota8("Teplota8", HASensorNumber::PrecisionP1);
HASensorNumber teplota9("Teplota9", HASensorNumber::PrecisionP1);
HASensorNumber teplota10("Teplota10", HASensorNumber::PrecisionP1);
HASensorNumber teplota11("Teplota11", HASensorNumber::PrecisionP1);

HASensorNumber teplotadht("teplotadht", HASensorNumber::PrecisionP1);
HASensorNumber vlhkostdht("vlhkostdht", HASensorNumber::PrecisionP1);

HASwitch cirk("Cirkul");
HABinarySensor hladina("Hladin");

// Stav hladiny pro omezení publikace
bool lastHladinaState = false;

// ====== Deklarace ======
void GUI();
void setup_display();
void setup_sensors();
void setup_ha_device();
void CirkulaceCommand(bool state, HASwitch* sender);
bool parseSerialPowers(String& data);

// ====== ISR ======
void IRAM_ATTR stisknuto() {
  unsigned long now = millis();
  // Debounce: ignoruj přerušení příliš brzy po sobě
  if ((now - lastInterruptTime) > DEBOUNCE_MS) {
    Displej = now;          // probuzení displeje
    lastInterruptTime = now;
  }
}

// ====== Nastavení HA entity, displeje a senzorů ======
void setup_display() {
  Wire.begin();  // zajistí inicializaci I2C
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
}

void setup_sensors() {
  sensors.begin();
  numberOfDevices = sensors.getDeviceCount();
}

void setup_ha_device() {
  device.setName("Kotelna měření");
  device.setSoftwareVersion("1.1");
  device.setManufacturer("Black Corp.");
  device.setModel("Emon v1.2");
  device.enableSharedAvailability();
  device.enableLastWill();

  // Ikony a jednotky – výkon
  vykon0.setName("Tepelné čerpadlo");
  vykon0.setUnitOfMeasurement("W");
  vykon0.setIcon("mdi:flash");

  vykon1.setName("Kompresor");
  vykon1.setUnitOfMeasurement("W");
  vykon1.setIcon("mdi:flash");

  vykon2.setName("Vnitřní jednotka");
  vykon2.setUnitOfMeasurement("W");
  vykon2.setIcon("mdi:flash");

  vykon3.setName("Patrona 1");
  vykon3.setUnitOfMeasurement("W");
  vykon3.setIcon("mdi:flash");

  vykon4.setName("Patrona 2");
  vykon4.setUnitOfMeasurement("W");
  vykon4.setIcon("mdi:flash");

  vykon5.setName("Patrona 3");
  vykon5.setUnitOfMeasurement("W");
  vykon5.setIcon("mdi:flash");

  vykon6.setName("Výkon6");
  vykon6.setUnitOfMeasurement("W");
  vykon6.setIcon("mdi:flash");

  vykon7.setName("Výkon7");
  vykon7.setUnitOfMeasurement("W");
  vykon7.setIcon("mdi:flash");

  vykon8.setName("Výkon8");
  vykon8.setUnitOfMeasurement("W");
  vykon8.setIcon("mdi:flash");

  // Teploty – ikony, jednotky
  teplota1.setName("Teplota1"); teplota1.setUnitOfMeasurement("°C"); teplota1.setIcon("mdi:thermometer");
  teplota2.setName("Teplota2"); teplota2.setUnitOfMeasurement("°C"); teplota2.setIcon("mdi:thermometer");
  teplota3.setName("Teplota3"); teplota3.setUnitOfMeasurement("°C"); teplota3.setIcon("mdi:thermometer");
  teplota4.setName("Teplota4"); teplota4.setUnitOfMeasurement("°C"); teplota4.setIcon("mdi:thermometer");
  teplota5.setName("Teplota5"); teplota5.setUnitOfMeasurement("°C"); teplota5.setIcon("mdi:thermometer");
  teplota6.setName("Teplota6"); teplota6.setUnitOfMeasurement("°C"); teplota6.setIcon("mdi:thermometer");
  teplota7.setName("Teplota7"); teplota7.setUnitOfMeasurement("°C"); teplota7.setIcon("mdi:thermometer");
  teplota8.setName("Teplota8"); teplota8.setUnitOfMeasurement("°C"); teplota8.setIcon("mdi:thermometer");
  teplota9.setName("Teplota9"); teplota9.setUnitOfMeasurement("°C"); teplota9.setIcon("mdi:thermometer");
  teplota10.setName("Teplota10"); teplota10.setUnitOfMeasurement("°C"); teplota10.setIcon("mdi:thermometer");
  teplota11.setName("Teplota11"); teplota11.setUnitOfMeasurement("°C"); teplota11.setIcon("mdi:thermometer");

  // Spínač cirkulace
  cirk.setIcon("mdi:water-pump");
  cirk.setName("Cirkulace");
  cirk.onCommand(CirkulaceCommand);

  // Hladina
  hladina.setName("Hladina");
  hladina.setIcon("mdi:water-alert");

  // DHT
  teplotadht.setName("Teplota Kotelna");
  teplotadht.setUnitOfMeasurement("°C");
  teplotadht.setIcon("mdi:thermometer");

  vlhkostdht.setName("Vlhkost Kotelna");
  vlhkostdht.setUnitOfMeasurement("%");
  vlhkostdht.setIcon("mdi:water-percent");
}

// ====== Command handler ======
void CirkulaceCommand(bool state, HASwitch* sender) {
  digitalWrite(Cirk_PIN, (state ? HIGH : LOW));
  sender->setState(state); // report state back to Home Assistant
  stavc = state ? 1 : 0;
}

// ====== Web GUI ======
void GUI() {
  display.clearDisplay();

  // Po 10 minutách se display „vypne“ (zůstane černý)
  if (millis() - Displej > 600000) {
    display.display();
    return;
  }  

  unsigned long odpocet = 600 - ((millis() - Displej) / 1000);

  display.setCursor(0,0);
  display.print(numberOfDevices);
  display.print(" sensors");  

  display.setCursor(62,0);
  display.print(odpocet);

  int sig = WiFi.RSSI();
  if (sig > -55) display.fillRect(125,1,2,6,WHITE);
  if (sig > -65) display.fillRect(122,2,2,5,WHITE);
  if (sig > -70) display.fillRect(119,3,2,4,WHITE);
  if (sig > -78) display.fillRect(116,4,2,3,WHITE);
  if (sig > -82) display.fillRect(113,5,2,2,WHITE);

  display.setCursor(0,15);
  display.print("TC: "); display.print(P0); display.println("W");
  display.print("Kompresor: "); display.print(P1); display.println("W");
  display.print("Inside: "); display.print(P2); display.println("W");
  display.print("Cirkulace: "); display.println(stavc == 1 ? "ON" : "OFF");

  display.setCursor(0, 56);
  display.print(WiFi.localIP());

  display.display();
}

// ====== Setup ======
void setup() {
  pinMode(Cirk_PIN, OUTPUT);
  digitalWrite(Cirk_PIN, LOW);
  pinMode(Hladina_PIN, INPUT);
  pinMode(TlacPin, INPUT_PULLDOWN);

  Serial.begin(9600);

  setup_display();
  setup_ha_device();
  setup_sensors();

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);

  // Počkáme na Wi-Fi
  while (WiFi.status() != WL_CONNECTED) {
    display.clearDisplay();
    display.setCursor(20,25);
    display.print("WIFI Connecting");
    display.display();
    delay(500);
  }

  display.clearDisplay();
  display.setCursor(20,25);
  display.print("WIFI Connected!");
  display.display();
  delay(1000);

  // Web server (ElegantOTA landing)
    server.on("/", []() {
    String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>OTA</title></head>"
                "<body style='background-color:#000000;color:#ffffff;font-family:sans-serif;text-align:center;margin-top:50px;'>"
                "<h1>ElegantOTA Demo</h1>"
                "<p><a href=\"/update\" style='font-size:20px;'>Klikni sem pro aktualizaci firmware</a></p>"
                "</body></html>";
    server.send(200, "text/html", html);
    });

  ElegantOTA.begin(&server);
  server.begin();

  mqtt.begin(BROKER_ADDR);

  dht.begin();

  attachInterrupt(digitalPinToInterrupt(TlacPin), stisknuto, RISING);

  // inicializace stavu hladiny
  lastHladinaState = !digitalRead(Hladina_PIN);
  hladina.setState(lastHladinaState);

  // Probuzení displeje na startu
  Displej = millis();

  // DHT okno (minuta)
  dhtWindowStartMillis = millis();
}

// ====== Pomocná funkce: bezpečné parsování Serialu ======
bool parseSerialPowers(String& data) {
  // očekáváme 8 hodnot oddělených čárkou, bez negativních hodnot
  int commas = 0;
  for (size_t i = 0; i < data.length(); i++) if (data[i] == ',') commas++;
  if (commas < 7) return false;

  int index1 = data.indexOf(',');
  int index2 = data.indexOf(',', index1 + 1);
  int index3 = data.indexOf(',', index2 + 1);
  int index4 = data.indexOf(',', index3 + 1);
  int index5 = data.indexOf(',', index4 + 1);
  int index6 = data.indexOf(',', index5 + 1);
  int index7 = data.indexOf(',', index6 + 1);

  // Hodnoty jsou proudy (A)? -> násobíme 230 pro výkon (W)
  P1 = data.substring(0, index1).toInt() * 230UL;
  P2 = data.substring(index1 + 1, index2).toInt() * 230UL;
  P3 = data.substring(index2 + 1, index3).toInt() * 230UL;
  P4 = data.substring(index3 + 1, index4).toInt() * 230UL;
  P5 = data.substring(index4 + 1, index5).toInt() * 230UL;
  P6 = data.substring(index5 + 1, index6).toInt() * 230UL;
  P7 = data.substring(index6 + 1, index7).toInt() * 230UL;
  P8 = data.substring(index7 + 1).toInt() * 230UL;

  // P0 = součet P1-P5 (dle tvého požadavku)
  P0 = P1 + P2 + P3 + P4 + P5;

  return true;
}

// ====== Loop ======
void loop() {
  server.handleClient();
  ElegantOTA.loop();

  // MQTT loop volat často kvůli udržení spojení
  mqtt.loop();

  // Wi-Fi reconnect bez delay
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastWiFiReconnectAttempt > WIFI_RECONNECT_INTERVAL_MS) {
      WiFi.reconnect();
      lastWiFiReconnectAttempt = millis();
    }
  }

  // ------- DHT čtení a průměrování -------
  // čteme často, ale publikujeme jen jednou za minutu
  h = dht.readHumidity();
  t = dht.readTemperature();

  if (!isnan(t) && !isnan(h)) {
    dhtSumT += t;
    dhtSumH += h;
    dhtCount++;
  }

  // Po 60 s spočítej průměr a připrav k publikaci
  unsigned long now = millis();
  if (now - dhtWindowStartMillis >= 60000UL) {
    if (dhtCount > 0) {
      dhtAvgT = (float)(dhtSumT / (double)dhtCount);
      dhtAvgH = (float)(dhtSumH / (double)dhtCount);
      dhtNewMinuteReady = true; // připraveno k publikaci (jen jednou)
    } else {
      // žádný validní vzorek v okně -> označíme jako nedostupné při publikaci
      dhtAvgT = NAN;
      dhtAvgH = NAN;
      dhtNewMinuteReady = true; // publikujeme nedostupnost
    }
    // reset okna
    dhtSumT = 0.0;
    dhtSumH = 0.0;
    dhtCount = 0;
    dhtWindowStartMillis = now;
  }

  // Hladina – publikovat jen při změně (aby se server nezatěžoval)
  bool currentHladina = !digitalRead(Hladina_PIN);
  if (currentHladina != lastHladinaState) {
    lastHladinaState = currentHladina;
    hladina.setState(lastHladinaState);
  }

  // Serial parsing: přijde řádek s 8 hodnotami oddělenými čárkami
  if (Serial.available() > 0) {
    String data = Serial.readStringUntil('\n');
    data.trim();
    parseSerialPowers(data); // bezpečně naplní P1..P8 a P0, pokud je formát validní
  }

  // GUI (displej)
  GUI();

  // ------- Publikace do HA v intervalu -------
  currentMillis = millis();
  if (currentMillis - previousPublishMillis >= INTERVAL) {
    previousPublishMillis = currentMillis;

    // Aktualizace počtu 1-Wire zařízení (může se změnit)
    numberOfDevices = sensors.getDeviceCount();
    sensors.requestTemperatures();

    // DS18B20 – dynamická dostupnost + publikace hodnot
    for (int i = 0; i < 11; i++) {
      float tempC = (i < numberOfDevices) ? sensors.getTempCByIndex(i) : DEVICE_DISCONNECTED_C;
      bool available = (tempC != DEVICE_DISCONNECTED_C);

      switch (i) {
        case 0:  teplota1.setAvailability(available); if (available) teplota1.setValue(tempC);  break;
        case 1:  teplota2.setAvailability(available); if (available) teplota2.setValue(tempC);  break;
        case 2:  teplota3.setAvailability(available); if (available) teplota3.setValue(tempC);  break;
        case 3:  teplota4.setAvailability(available); if (available) teplota4.setValue(tempC);  break;
        case 4:  teplota5.setAvailability(available); if (available) teplota5.setValue(tempC);  break;
        case 5:  teplota6.setAvailability(available); if (available) teplota6.setValue(tempC);  break;
        case 6:  teplota7.setAvailability(available); if (available) teplota7.setValue(tempC);  break;
        case 7:  teplota8.setAvailability(available); if (available) teplota8.setValue(tempC);  break;
        case 8:  teplota9.setAvailability(available); if (available) teplota9.setValue(tempC);  break;
        case 9:  teplota10.setAvailability(available); if (available) teplota10.setValue(tempC); break;
        case 10: teplota11.setAvailability(available); if (available) teplota11.setValue(tempC); break;
      }
    }

    // DHT – publikace PRŮMĚRU JEDNOU ZA MINUTU
    if (dhtNewMinuteReady) {
      if (!isnan(dhtAvgT)) { teplotadht.setAvailability(true); teplotadht.setValue(dhtAvgT); }
      else                 { teplotadht.setAvailability(false); }

      if (!isnan(dhtAvgH)) { vlhkostdht.setAvailability(true); vlhkostdht.setValue(dhtAvgH); }
      else                 { vlhkostdht.setAvailability(false); }

      dhtNewMinuteReady = false; // publikováno, čekáme na další minutu
    }
    // Pozn.: pokud dhtNewMinuteReady == false, hodnoty DHT necháváme beze změny,
    // takže v HA neuvidíš update častěji než jednou za minutu.

    // Výkony – publikace jen v intervalu (5 s)
    vykon0.setValue((uint32_t)P0);
    vykon1.setValue((uint32_t)P1);
    vykon2.setValue((uint32_t)P2);
    vykon3.setValue((uint32_t)P3);
    vykon4.setValue((uint32_t)P4);
    vykon5.setValue((uint32_t)P5);
    vykon6.setValue((uint32_t)P6);
    vykon7.setValue((uint32_t)P7);
    vykon8.setValue((uint32_t)P8);
  }
}
