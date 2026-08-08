#include <Arduino.h>
#include "config.h"
#include "RadarLD2450.h"

HardwareSerial RadarSerial(1);
RadarLD2450 radar;

unsigned long ultimaImpresion = 0;
bool radarEstabaActivo = true;

struct EstadoInmovilidad {
  bool temporizadorActivo = false;
  bool alertaEnviada = false;
  unsigned long inicioQuieto = 0;
};

EstadoInmovilidad estado[MAX_OBJETIVOS];

void manejarInmovilidad(int i, const Objetivo &obj) {
  bool quieto = abs(obj.velocidad) <= UMBRAL_VELOCIDAD_QUIETO;

  if (!obj.presente) {
    estado[i] = EstadoInmovilidad(); // reset si el objetivo desaparece
    return;
  }

  if (quieto) {
    if (!estado[i].temporizadorActivo) {
      estado[i].temporizadorActivo = true;
      estado[i].inicioQuieto = millis();
      estado[i].alertaEnviada = false;
      Serial.printf("[INFO] Obj %d inmovil. Iniciando conteo...\n", i + 1);
    } else if (!estado[i].alertaEnviada &&
               millis() - estado[i].inicioQuieto >= TIEMPO_ESPERA_ALERTA_MS) {
      Serial.printf(">>> ALERTA: Obj %d inmovil por 3+ segundos. Posible caida. <<<\n", i + 1);
      estado[i].alertaEnviada = true;
      // TODO fase notificaciones: enviarAlerta(i, obj);
    }
  } else {
    if (estado[i].temporizadorActivo) {
      Serial.printf("[INFO] Obj %d en movimiento. Alerta cancelada.\n", i + 1);
    }
    estado[i] = EstadoInmovilidad();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n# Wi-Care: Deteccion de inmovilidad (v4)...");
  radar.begin(RadarSerial);
}

void loop() {
  radar.update();

  // Chequeo de salud del radar (desconexion / falla)
  bool radarActivoAhora = radar.radarActivo();
  if (radarActivoAhora != radarEstabaActivo) {
    Serial.println(radarActivoAhora
      ? "[INFO] Radar reconectado / enviando datos."
      : "[ALERTA SISTEMA] Sin datos del radar - posible desconexion.");
    radarEstabaActivo = radarActivoAhora;
  }

  if (!radar.frameNuevoDisponible()) return;
  radar.limpiarFlagFrameNuevo();

  bool imprimir = (millis() - ultimaImpresion >= INTERVALO_IMPRESION_MS);
  if (imprimir) ultimaImpresion = millis();

  for (int i = 0; i < MAX_OBJETIVOS; i++) {
    const Objetivo &obj = radar.objetivo(i);
    if (imprimir && obj.presente) {
      Serial.printf("Obj %d -> X=%-6d mm Y=%-6d mm V=%-6d cm/s\n", i + 1, obj.x, obj.y, obj.velocidad);
    }
    manejarInmovilidad(i, obj);
  }
}