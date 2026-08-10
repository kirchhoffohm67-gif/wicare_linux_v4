#include <Arduino.h>
#include "config.h"
#include "RadarLD2450.h"

HardwareSerial RadarSerial(1);
RadarLD2450 radar;

unsigned long ultimaImpresion = 0;
bool radarEstabaActivo = true;

#define SALTO_MAXIMO_MM 400
#define FRAMES_PARA_CANCELAR_QUIETO 3

struct EstadoObjetivo {
  float xFiltrado = 0, yFiltrado = 0, vFiltrado = 0;
  bool filtroInicializado = false;

  uint8_t framesPresente = 0;
  uint8_t framesAusente = 0;
  bool presenciaConfirmada = false;

  // Inactividad general
  bool temporizadorActivo = false;
  bool alertaInactividadEnviada = false;
  unsigned long inicioQuieto = 0;
  float xInicioQuieto = 0, yInicioQuieto = 0;
  uint8_t framesMovimiento = 0;

  // Burst / confirmacion de caida (independiente de inactividad general)
  unsigned long ultimoMovimientoBrusco = 0;
  unsigned long ultimaAlertaCaida = 0;
  int16_t velocidadUltimoBurst = 0;
  int16_t xUltimoBurst = 0;
  int16_t yUltimoBurst = 0;
  uint8_t framesBrusco = 0;
  bool enConfirmacionCaida = false;
  unsigned long inicioConfirmacionCaida = 0;
  uint8_t framesRecuperacion = 0;
};

EstadoObjetivo estado[MAX_OBJETIVOS];

void resetEstado(EstadoObjetivo &e) {
  unsigned long guardarMovimientoBrusco = e.ultimoMovimientoBrusco;
  unsigned long guardarUltimaAlerta = e.ultimaAlertaCaida;
  bool guardarEnConfirmacion = e.enConfirmacionCaida;
  unsigned long guardarInicioConfirmacion = e.inicioConfirmacionCaida;
  int16_t guardarVelocidadBurst = e.velocidadUltimoBurst;
  int16_t guardarXBurst = e.xUltimoBurst;
  int16_t guardarYBurst = e.yUltimoBurst;

  e = EstadoObjetivo();

  // Preserva memoria de caida/confirmacion por si el objetivo se perdio
  // brevemente justo durante el impacto (glitch de reconexion).
  e.ultimoMovimientoBrusco = guardarMovimientoBrusco;
  e.ultimaAlertaCaida = guardarUltimaAlerta;
  e.enConfirmacionCaida = guardarEnConfirmacion;
  e.inicioConfirmacionCaida = guardarInicioConfirmacion;
  e.velocidadUltimoBurst = guardarVelocidadBurst;
  e.xUltimoBurst = guardarXBurst;
  e.yUltimoBurst = guardarYBurst;
}

bool esSaltoImposible(const EstadoObjetivo &e, const Objetivo &obj) {
  if (!e.filtroInicializado) return false;
  float dx = obj.x - e.xFiltrado;
  float dy = obj.y - e.yFiltrado;
  return sqrtf(dx * dx + dy * dy) > SALTO_MAXIMO_MM;
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

void manejarInmovilidadYCaida(int i, EstadoObjetivo &e, const Objetivo &obj, bool imprimir) {
  // --- 1. Deteccion de burst (movimiento brusco) ---
  bool esteFrameBrusco = fabs(obj.velocidad) >= VELOCIDAD_MOVIMIENTO_BRUSCO;
  e.framesBrusco = esteFrameBrusco ? (e.framesBrusco + 1) : 0;

  bool nuevoBurstConfirmado = false;
  if (e.framesBrusco >= FRAMES_BRUSCO_CONFIRMAR) {
    if (e.ultimoMovimientoBrusco == 0 || millis() - e.ultimoMovimientoBrusco > 200) {
      Serial.printf("[BURST] Obj %d - movimiento brusco confirmado (v=%d cm/s)\n", i + 1, obj.velocidad);
      nuevoBurstConfirmado = true;
    }
    e.ultimoMovimientoBrusco = millis();
    e.velocidadUltimoBurst = obj.velocidad;
    e.xUltimoBurst = obj.x;
    e.yUltimoBurst = obj.y;
  }

  bool enCooldown = (millis() - e.ultimaAlertaCaida) < COOLDOWN_ALERTA_MS;

  if (nuevoBurstConfirmado && !e.enConfirmacionCaida && !enCooldown) {
    e.enConfirmacionCaida = true;
    e.inicioConfirmacionCaida = millis();
    e.framesRecuperacion = 0;
    Serial.printf("[INFO] Obj %d - iniciando confirmacion de posible caida (%.0fs de ventana)...\n",
                  i + 1, TIEMPO_ESPERA_ALERTA_MS / 1000.0);
  }

  // --- 2. Maquina de confirmacion de caida (tolerante a micro-ajustes) ---
  if (e.enConfirmacionCaida) {
  unsigned long tiempoEnConfirmacion = millis() - e.inicioConfirmacionCaida;

  // Solo cuenta como "recuperacion" (persona se movio/levanto) si ocurre
  // DESPUES del margen minimo - los primeros ms son la caida misma asentandose.
  bool dentroDeMargenDeCaida = tiempoEnConfirmacion < TIEMPO_MINIMO_ANTES_RECUPERACION_MS;
  bool movimientoDeRecuperacion = !dentroDeMargenDeCaida && (fabs(obj.velocidad) >= VELOCIDAD_RECUPERACION_CM_S);
  e.framesRecuperacion = movimientoDeRecuperacion ? (e.framesRecuperacion + 1) : 0;

    Serial.printf("   [debug-caida] Obj %d confirmando=%.1fs framesRecuperacion=%d vRaw=%d\n",
                  i + 1, tiempoEnConfirmacion / 1000.0, e.framesRecuperacion, obj.velocidad);

    if (e.framesRecuperacion >= FRAMES_RECUPERACION_TRAS_CAIDA) {
      Serial.printf("[INFO] Obj %d se movio/recupero tras el burst - caida descartada.\n", i + 1);
      e.enConfirmacionCaida = false;
    } else if (tiempoEnConfirmacion >= TIEMPO_ESPERA_ALERTA_MS) {
      Serial.printf(">>> ALERTA CAIDA: Obj %d - inmovil %.1fs. Frame que disparo: X=%d Y=%d V=%d cm/s. Posicion actual: X=%.0f Y=%.0f. <<<\n",
                    i + 1, tiempoEnConfirmacion / 1000.0,
                    e.xUltimoBurst, e.yUltimoBurst, e.velocidadUltimoBurst,
                    e.xFiltrado, e.yFiltrado);
      e.enConfirmacionCaida = false;
      e.ultimaAlertaCaida = millis();
    }
  }

  // --- 3. Inactividad general (independiente de la confirmacion de caida) ---
  bool porEncimaUmbral = fabs(e.vFiltrado) > UMBRAL_VELOCIDAD_QUIETO;
  e.framesMovimiento = porEncimaUmbral ? (e.framesMovimiento + 1) : 0;
  bool quietoPorVelocidad = e.framesMovimiento < FRAMES_PARA_CANCELAR_QUIETO;

  if (!e.temporizadorActivo && quietoPorVelocidad) {
    e.temporizadorActivo = true;
    e.inicioQuieto = millis();
    e.xInicioQuieto = e.xFiltrado;
    e.yInicioQuieto = e.yFiltrado;
    e.alertaInactividadEnviada = false;
  }

  if (e.temporizadorActivo) {
    float desplazamiento = sqrtf(powf(e.xFiltrado - e.xInicioQuieto, 2) + powf(e.yFiltrado - e.yInicioQuieto, 2));
    bool sigueQuieto = quietoPorVelocidad && desplazamiento < DESPLAZAMIENTO_CANCELA_QUIETO_MM;

    if (!sigueQuieto) {
      e.temporizadorActivo = false;
      e.alertaInactividadEnviada = false;
    } else {
      unsigned long tiempoQuieto = millis() - e.inicioQuieto;
      if (!e.alertaInactividadEnviada && !e.enConfirmacionCaida && tiempoQuieto >= TIEMPO_INACTIVIDAD_LARGA_MS) {
        Serial.printf("[AVISO] Obj %d inactivo por mas de %lu ms.\n", i + 1, (unsigned long)TIEMPO_INACTIVIDAD_LARGA_MS);
        e.alertaInactividadEnviada = true;
      }
    }
  }

  if (imprimir) {
    Serial.printf("   [debug] Obj %d quieto=%d framesMov=%d vRaw=%d confirmandoCaida=%d\n",
                  i + 1, e.temporizadorActivo, e.framesMovimiento, obj.velocidad, e.enConfirmacionCaida);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n# Wi-Care: Deteccion de caidas (v4.4 - confirmacion de caida separada de inactividad)...");
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

    if (obj.presente) {
      actualizarFiltro(e, obj);
      manejarInmovilidadYCaida(i, e, obj, imprimir);

      if (imprimir) {
        Serial.printf("Obj %d -> X=%-6.0f mm Y=%-6.0f mm V=%-6.1f cm/s\n",
                      i + 1, e.xFiltrado, e.yFiltrado, e.vFiltrado);
      }
    }
  }
}