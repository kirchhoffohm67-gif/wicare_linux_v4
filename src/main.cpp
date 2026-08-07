#include <Arduino.h>

#define RX_PIN 18
#define TX_PIN 17

HardwareSerial Radar(1);

static const uint8_t HDR[4] = {0xAA, 0xFF, 0x03, 0x00};
uint8_t buf[30];
uint8_t idx = 0;

unsigned long ultimaImpresion = 0;
const unsigned long INTERVALO_IMPRESION = 300;

// Variables de control de inmovilidad por objetivo (hasta 3 posibles)
unsigned long tiempoQuieto[3] = {0, 0, 0};
bool temporizadorActivo[3] = {false, false, false};
const unsigned long TIEMPO_ESPERA_ALERTA = 3000; // 3 segundos quieto
const int16_t UMBRAL_VELOCIDAD = 5; // cm/s, tolerancia de "quieto"

int16_t decodificar(uint16_t raw) {
  if (raw & 0x8000) {
    return (int16_t)(raw & 0x7FFF);
  } else {
    return -(int16_t)raw;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n# Wi-Care: Deteccion de Caidas (datos corregidos)...");
  Radar.begin(256000, SERIAL_8N1, RX_PIN, TX_PIN);
}

void loop() {
  while (Radar.available()) {
    uint8_t b = Radar.read();

    if (idx < 4) {
      if (b == HDR[idx]) buf[idx++] = b;
      else { idx = (b == HDR[0]) ? 1 : 0; if (idx) buf[0] = HDR[0]; }
      continue;
    }
    buf[idx++] = b;

    if (idx == 30) {
      if (buf[28] == 0x55 && buf[29] == 0xCC) {

        bool imprimir = (millis() - ultimaImpresion >= INTERVALO_IMPRESION);
        if (imprimir) ultimaImpresion = millis();

        for (int i = 0; i < 3; i++) {
          int offset = 4 + (i * 8);
          uint16_t rawX = buf[offset]   | (buf[offset+1] << 8);
          uint16_t rawY = buf[offset+2] | (buf[offset+3] << 8);
          uint16_t rawV = buf[offset+4] | (buf[offset+5] << 8);

          int16_t x = decodificar(rawX);
          int16_t y = decodificar(rawY);
          int16_t v = decodificar(rawV);

          bool hayObjetivo = (x != 0 || y != 0);

          if (imprimir && hayObjetivo) {
            Serial.printf("Obj %d -> X=%-6d mm Y=%-6d mm V=%-6d cm/s\n", i+1, x, y, v);
          }

          if (hayObjetivo) {
            // Si la velocidad esta dentro del umbral de "quieto"
            if (abs(v) <= UMBRAL_VELOCIDAD) {
              if (!temporizadorActivo[i]) {
                temporizadorActivo[i] = true;
                tiempoQuieto[i] = millis();
                Serial.printf("[INFO] Obj %d inmovil. Iniciando conteo...\n", i+1);
              } else if (millis() - tiempoQuieto[i] >= TIEMPO_ESPERA_ALERTA) {
                Serial.printf(">>> ¡ALERTA CRITICA: Obj %d inmovil por 3+ segundos! Posible caida. <<<\n", i+1);
                tiempoQuieto[i] = millis() + 10000; // evita repetir cada ciclo
              }
            } else {
              if (temporizadorActivo[i]) {
                Serial.printf("[INFO] Obj %d en movimiento. Alerta cancelada.\n", i+1);
              }
              temporizadorActivo[i] = false;
            }
          } else {
            temporizadorActivo[i] = false;
          }
        }
      }
      idx = 0;
    }
  }
}