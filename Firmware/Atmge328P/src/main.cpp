#include <Arduino.h>
#include <EmonLib.h>       // Knihovna EmonLib pro měření proudu


EnergyMonitor emon1, emon2, emon3, emon4, emon5, emon6, emon7, emon8;  // Pro každý ADC pin vytvoříme instanci

const unsigned long INTERVAL = 5000;  // 5 sekundový interval mezi měřeními
const unsigned long SEND_INTERVAL = 60000;  // 1 minuta pro odeslání hodnot

unsigned long previousMillis = 0;
unsigned long sendMillis = 0;

// Pole pro ukládání průměrných hodnot pro každý kanál
float totalCurrent[8] = {0, 0, 0, 0, 0, 0, 0, 0};
int sampleCount = 0;

void setup() {
  Serial.begin(9600);  // Inicializace sériové komunikace
  
  // Inicializace každého kanálu (každý odpovídá konkrétnímu ADC pinu)
  emon1.current(A0, 19.6);  // Kalibrace pro každý kanál
  emon2.current(A1, 19.6);
  emon3.current(A2, 19.6);
  emon4.current(A3, 19.6);
  emon5.current(A4, 19.6);
  emon6.current(A5, 19.6);
  emon7.current(A6, 19.6);
  emon8.current(A7, 19.6);
}

void loop() {
  unsigned long currentMillis = millis();

  // Měření každých 5 sekund
  if (currentMillis - previousMillis >= INTERVAL) {
    previousMillis = currentMillis;

    // Měření proudu na všech kanálech
    totalCurrent[0] += emon1.calcIrms(1480);  // Měření na A0
    totalCurrent[1] += emon2.calcIrms(1480);  // Měření na A1
    totalCurrent[2] += emon3.calcIrms(1480);  // Měření na A2
    totalCurrent[3] += emon4.calcIrms(1480);  // Měření na A3
    totalCurrent[4] += emon5.calcIrms(1480);  // Měření na A4
    totalCurrent[5] += emon6.calcIrms(1480);  // Měření na A5
    totalCurrent[6] += emon7.calcIrms(1480);  // Měření na A6
    totalCurrent[7] += emon8.calcIrms(1480);  // Měření na A7

    sampleCount++;  // Počet vzorků měření
  }

  // Odesílání průměrných hodnot jednou za minutu ve formátu CSV
  if (currentMillis - sendMillis >= SEND_INTERVAL) {
    sendMillis = currentMillis;

    // Vytvoření CSV řetězce
    String csvData = "";  // Proměnná pro uložení všech hodnot

    for (int i = 0; i < 8; i++) {
      float averageCurrent = totalCurrent[i] / sampleCount;  // Výpočet průměru
      csvData += String(averageCurrent, 3);  // Přidání hodnoty do CSV s přesností na 3 desetinná místa

      if (i < 7) {
        csvData += ",";  // Přidej čárku mezi hodnoty
      }
    }

    // Odeslání celého řetězce najednou
    Serial.println(csvData);

    // Resetování pro další měření
    for (int i = 0; i < 8; i++) {
      totalCurrent[i] = 0;
    }

    sampleCount = 0;  // Reset počtu vzorků
  }
}
