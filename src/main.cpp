
#include <Arduino.h>
#include "config.h"
#include "RadarLD2450.h"

HardwareSerial RadarSerial(1);
RadarLD2450 radar;

unsigned long ultimaImpresion = 0;
bool radarEstabaActivo = true;

// Umbral de salto imposible entre frames consecutivos (~10 frames/seg del radar).
// Si un objetivo "salta" mas de esto en 100ms, se descarta como glitch de reconexion.
#define SALTO_MAXIMO_MM 400

// Frames consecutivos de velocidad alta necesarios para cancelar el estado "quieto"
// (evita que un solo frame de ruido reinicie el conteo de 3s o 60s)
#define FRAMES_PARA_CANCELAR_QUIETO 3

struct EstadoObjetivo {
  float xFiltrado = 0, yFiltrado = 0, vFiltrado = 0;
  bool filtroInicializado = false;

  uint8_t framesPresente = 0;
  uint8_t framesAusente = 0;
  bool presenciaConfirmada = false;

  bool temporizadorActivo = false;
  bool alertaCaidaEnviada = false;
  bool alertaInactividadEnviada = false;
  unsigned long inicioQuieto = 0;
  unsigned long ultimoMovimientoBrusco = 0; // se preserva entre reconexiones cortas
  uint8_t framesMovimiento = 0;
};

EstadoObjetivo estado[MAX_OBJETIVOS];

void resetEstado(EstadoObjetivo &e) {
  unsigned long guardarMovimientoBrusco = e.ultimoMovimientoBrusco;
  e = EstadoObjetivo();
  e.ultimoMovimientoBrusco = guardarMovimientoBrusco; // no perder la memoria de la caida
}

bool esSaltoImposible(const EstadoObjetivo &e, const Objetivo &obj) {
  if (!e.filtroInicializado) return false;
  float dx = obj.x - e.xFiltrado;
  float dy = obj.y - e.yFiltrado;
  float distancia = sqrtf(dx * dx + dy * dy);
  return distancia > SALTO_MAXIMO_MM;
}

void actualizarFiltro(EstadoObjetivo &e, const Objetivo &obj) {
  if (!e.filtroInicializado) {
    e.xFiltrado = obj.x;
    e.yFiltrado = obj.y;
    e.vFiltrado = obj.velocidad;
    e.filtroInicializado = true;
    return;
  }
  if (esSaltoImposible(e, obj)) {
    // Glitch de reconexion: ignora la posicion de este frame, pero si toma la velocidad
    // (puede ser real, coincide con la caida) para no perder el pico brusco.
    e.vFiltrado += FILTRO_ALPHA * (obj.velocidad - e.vFiltrado);
    return;
  }
  e.xFiltrado += FILTRO_ALPHA * (obj.x - e.xFiltrado);
  e.yFiltrado += FILTRO_ALPHA * (obj.y - e.yFiltrado);
  e.vFiltrado += FILTRO_ALPHA * (obj.velocidad - e.vFiltrado);
}

void manejarPresencia(EstadoObjetivo &e, bool rawPresente) {
  if (rawPresente) {
    e.framesPresente++;
    e.framesAusente = 0;
    if (!e.presenciaConfirmada && e.framesPresente >= PRESENCE_CONFIRM_FRAMES) {
      e.presenciaConfirmada = true;
    }
  } else {
    e.framesAusente++;
    e.framesPresente = 0;
    if (e.presenciaConfirmada && e.framesAusente >= PRESENCE_LOST_FRAMES) {
      resetEstado(e);
    }
  }
}

void manejarInmovilidadYCaida(int i, EstadoObjetivo &e, bool imprimir) {
  bool huboMovimientoBrusco = fabs(e.vFiltrado) >= VELOCIDAD_MOVIMIENTO_BRUSCO;
  if (huboMovimientoBrusco) {
    e.ultimoMovimientoBrusco = millis();
  }

  // Hysteresis: solo cancela "quieto" tras varios frames seguidos de velocidad alta
  bool porEncimaUmbral = fabs(e.vFiltrado) > UMBRAL_VELOCIDAD_QUIETO;
  e.framesMovimiento = porEncimaUmbral ? (e.framesMovimiento + 1) : 0;
  bool quieto = e.framesMovimiento < FRAMES_PARA_CANCELAR_QUIETO;

  if (quieto) {
    if (!e.temporizadorActivo) {
      e.temporizadorActivo = true;
      e.inicioQuieto = millis();
      e.alertaCaidaEnviada = false;
      e.alertaInactividadEnviada = false;
    } else {
      unsigned long tiempoQuieto = millis() - e.inicioQuieto;
      bool movimientoBruscoReciente = (millis() - e.ultimoMovimientoBrusco) <= VENTANA_CAIDA_MS;

      if (imprimir) {
        Serial.printf("   [debug] Obj %d quieto=%.1fs brusco_reciente=%d\n",
                      i + 1, tiempoQuieto / 1000.0, movimientoBruscoReciente);
      }

      if (!e.alertaCaidaEnviada && movimientoBruscoReciente && tiempoQuieto >= TIEMPO_ESPERA_ALERTA_MS) {
        Serial.printf(">>> ALERTA CAIDA: Obj %d - movimiento brusco seguido de inmovilidad. <<<\n", i + 1);
        e.alertaCaidaEnviada = true;
      } else if (!e.alertaInactividadEnviada && !movimientoBruscoReciente && tiempoQuieto >= TIEMPO_INACTIVIDAD_LARGA_MS) {
        Serial.printf("[AVISO] Obj %d inactivo por mas de %lu ms (sin movimiento brusco previo).\n",
                      i + 1, (unsigned long)TIEMPO_INACTIVIDAD_LARGA_MS);
        e.alertaInactividadEnviada = true;
      }
    }
  } else {
    e.temporizadorActivo = false;
    e.alertaCaidaEnviada = false;
    e.alertaInactividadEnviada = false;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n# Wi-Care: Deteccion de caidas (v4, con filtrado, debounce y anti-glitch)...");
  radar.begin(RadarSerial);
}

void loop() {
  radar.update();

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
    EstadoObjetivo &e = estado[i];

    manejarPresencia(e, obj.presente);
    if (!e.presenciaConfirmada) continue;

    actualizarFiltro(e, obj);
    manejarInmovilidadYCaida(i, e, imprimir);

    if (imprimir) {
      Serial.printf("Obj %d -> X=%-6.0f mm Y=%-6.0f mm V=%-6.1f cm/s\n",
                    i + 1, e.xFiltrado, e.yFiltrado, e.vFiltrado);
    }
  }
}