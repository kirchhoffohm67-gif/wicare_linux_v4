#include <Arduino.h>
#include "config.h"
#include "RadarLD2450.h"

HardwareSerial RadarSerial(1);
RadarLD2450 radar;

unsigned long ultimaImpresion = 0;
bool radarEstabaActivo = true;

#define SALTO_MAXIMO_MM 600
#define FRAMES_PARA_CANCELAR_QUIETO 3

struct EstadoObjetivo {
  float xFiltrado = 0, yFiltrado = 0, vFiltrado = 0;
  bool filtroInicializado = false;

  uint8_t framesPresente = 0;
  uint8_t framesAusente = 0;
  bool presenciaConfirmada = false;

  bool temporizadorActivo = false;
  bool alertaInactividadEnviada = false;
  unsigned long inicioQuieto = 0;
  float xInicioQuieto = 0, yInicioQuieto = 0;
  uint8_t framesMovimiento = 0;

  unsigned long ultimoMovimientoBrusco = 0;
  unsigned long ultimaAlertaCaida = 0;
  int16_t velocidadUltimoBurst = 0;
  int16_t xUltimoBurst = 0;
  int16_t yUltimoBurst = 0;
  uint8_t framesBrusco = 0;
  bool enConfirmacionCaida = false;
  unsigned long inicioConfirmacionCaida = 0;
  uint8_t framesRecuperacion = 0;

  bool baseRecuperacionCapturada = false;
  float xBaseRecuperacion = 0, yBaseRecuperacion = 0;

  float energiaPostImpacto = 0;
};

struct RegistroPerdido {
  bool activo = false;
  float x = 0, y = 0;
  unsigned long tiempoPerdida = 0;

  bool estabaEnConfirmacion = false;
  unsigned long inicioConfirmacionCaida = 0;
  bool baseRecuperacionCapturada = false;
  float xBaseRecuperacion = 0, yBaseRecuperacion = 0;
  float energiaPostImpacto = 0;
  int16_t xUltimoBurst = 0, yUltimoBurst = 0, velocidadUltimoBurst = 0;
  unsigned long ultimoMovimientoBrusco = 0;
  unsigned long ultimaAlertaCaida = 0;
};

EstadoObjetivo estado[MAX_OBJETIVOS];
RegistroPerdido registrosPerdidos[MAX_OBJETIVOS];

void guardarRegistroPerdido(const EstadoObjetivo &e) {
  int slot = -1;
  for (int j = 0; j < MAX_OBJETIVOS; j++) {
    if (!registrosPerdidos[j].activo) { slot = j; break; }
  }
  if (slot == -1) slot = 0;

  RegistroPerdido &r = registrosPerdidos[slot];
  r.activo = true;
  r.x = e.xFiltrado;
  r.y = e.yFiltrado;
  r.tiempoPerdida = millis();
  r.estabaEnConfirmacion = e.enConfirmacionCaida;
  r.inicioConfirmacionCaida = e.inicioConfirmacionCaida;
  r.baseRecuperacionCapturada = e.baseRecuperacionCapturada;
  r.xBaseRecuperacion = e.xBaseRecuperacion;
  r.yBaseRecuperacion = e.yBaseRecuperacion;
  r.energiaPostImpacto = e.energiaPostImpacto;
  r.xUltimoBurst = e.xUltimoBurst;
  r.yUltimoBurst = e.yUltimoBurst;
  r.velocidadUltimoBurst = e.velocidadUltimoBurst;
  r.ultimoMovimientoBrusco = e.ultimoMovimientoBrusco;
  r.ultimaAlertaCaida = e.ultimaAlertaCaida;
}

// Intenta fusionar un objetivo recien confirmado con uno perdido recientemente
// y cercano - evita tratar reconexiones del mismo radar-slot como "persona nueva".
bool intentarReidentificar(EstadoObjetivo &e, float xActual, float yActual) {
  for (int j = 0; j < MAX_OBJETIVOS; j++) {
    RegistroPerdido &r = registrosPerdidos[j];
    if (!r.activo) continue;
    if (millis() - r.tiempoPerdida > VENTANA_REIDENTIFICACION_MS) {
      r.activo = false;
      continue;
    }
    float dx = xActual - r.x;
    float dy = yActual - r.y;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < DISTANCIA_REIDENTIFICACION_MM) {
      e.enConfirmacionCaida = r.estabaEnConfirmacion;
      e.inicioConfirmacionCaida = r.inicioConfirmacionCaida;
      e.baseRecuperacionCapturada = r.baseRecuperacionCapturada;
      e.xBaseRecuperacion = r.xBaseRecuperacion;
      e.yBaseRecuperacion = r.yBaseRecuperacion;
      e.energiaPostImpacto = r.energiaPostImpacto;
      e.xUltimoBurst = r.xUltimoBurst;
      e.yUltimoBurst = r.yUltimoBurst;
      e.velocidadUltimoBurst = r.velocidadUltimoBurst;
      e.ultimoMovimientoBrusco = r.ultimoMovimientoBrusco;
      e.ultimaAlertaCaida = r.ultimaAlertaCaida;
      r.activo = false;
      Serial.printf("[INFO] Objetivo reidentificado tras perdida momentanea (%.0fmm) - continuando seguimiento, no es una persona nueva.\n", dist);
      return true;
    }
  }
  return false;
}

void resetEstado(EstadoObjetivo &e) {
  guardarRegistroPerdido(e);
  e = EstadoObjetivo();
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

void manejarPresencia(EstadoObjetivo &e, bool rawPresente, const Objetivo &obj) {
  if (rawPresente) {
    e.framesPresente++;
    e.framesAusente = 0;
    if (!e.presenciaConfirmada && e.framesPresente >= PRESENCE_CONFIRM_FRAMES) {
      e.presenciaConfirmada = true;
      // Justo al confirmar, intenta reidentificar contra objetivos perdidos recientes
      intentarReidentificar(e, obj.x, obj.y);
    }
  } else {
    e.framesAusente++;
    e.framesPresente = 0;
    if (e.presenciaConfirmada && e.framesAusente >= PRESENCE_LOST_FRAMES) {
      resetEstado(e);
    }
  }
}

void manejarInmovilidadYCaida(int i, EstadoObjetivo &e, const Objetivo &obj, bool imprimir, bool frameValido) {
  bool esteFrameBrusco = frameValido && (fabs(obj.velocidad) >= VELOCIDAD_MOVIMIENTO_BRUSCO);
  e.framesBrusco = esteFrameBrusco ? (e.framesBrusco + 1) : 0;

  bool burstPorFramesConsecutivos = e.framesBrusco >= FRAMES_BRUSCO_CONFIRMAR;
  bool burstInstantaneo = frameValido && (fabs(obj.velocidad) >= VELOCIDAD_BURST_INSTANTANEO);

  bool nuevoBurstConfirmado = false;
  if (burstPorFramesConsecutivos || burstInstantaneo) {
    if (e.ultimoMovimientoBrusco == 0 || millis() - e.ultimoMovimientoBrusco > 200) {
      Serial.printf("[BURST] Obj %d - movimiento brusco confirmado (v=%d cm/s, %s)\n",
                    i + 1, obj.velocidad, burstInstantaneo ? "instantaneo" : "sostenido");
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
    e.baseRecuperacionCapturada = false;
    e.energiaPostImpacto = 0;
    Serial.printf("[INFO] Obj %d - iniciando confirmacion de posible caida (%.0fs de ventana)...\n",
                  i + 1, TIEMPO_ESPERA_ALERTA_MS / 1000.0);
  }

  if (e.enConfirmacionCaida) {
    unsigned long tiempoEnConfirmacion = millis() - e.inicioConfirmacionCaida;
    bool dentroDeMargenDeCaida = tiempoEnConfirmacion < TIEMPO_MINIMO_ANTES_RECUPERACION_MS;

    if (!dentroDeMargenDeCaida && !e.baseRecuperacionCapturada) {
      e.xBaseRecuperacion = e.xFiltrado;
      e.yBaseRecuperacion = e.yFiltrado;
      e.baseRecuperacionCapturada = true;
    }

    bool movimientoDeRecuperacion = !dentroDeMargenDeCaida && (fabs(obj.velocidad) >= VELOCIDAD_RECUPERACION_CM_S);
    e.framesRecuperacion = movimientoDeRecuperacion ? (e.framesRecuperacion + 1) : 0;

    if (!dentroDeMargenDeCaida) {
      e.energiaPostImpacto += fabs(obj.velocidad);
    }

    float desplazamientoPostImpacto = 0;
    if (e.baseRecuperacionCapturada) {
      desplazamientoPostImpacto = sqrtf(powf(e.xFiltrado - e.xBaseRecuperacion, 2) +
                                         powf(e.yFiltrado - e.yBaseRecuperacion, 2));
    }

    // Unico criterio de cancelacion: energia Y desplazamiento juntos.
    // (Se elimino el atajo de "solo velocidad" - retorcerse/girar en el piso
    // genera velocidad de rotacion sin desplazamiento real, y no debe cancelar.)
    bool movimientoSostenido = e.energiaPostImpacto > ENERGIA_RECUPERACION_UMBRAL &&
                                e.baseRecuperacionCapturada &&
                                desplazamientoPostImpacto > DESPLAZAMIENTO_CANCELA_CONFIRMACION_MM;

    Serial.printf("   [debug-caida] Obj %d confirmando=%.1fs framesRecuperacion=%d vRaw=%d energia=%.0f desplazPostImpacto=%.0fmm\n",
                  i + 1, tiempoEnConfirmacion / 1000.0, e.framesRecuperacion, obj.velocidad,
                  e.energiaPostImpacto, desplazamientoPostImpacto);

    if (movimientoSostenido) {
      Serial.printf("[INFO] Obj %d se movio/recupero tras asentarse (energia=%.0f, desplazamiento=%.0fmm) - caida descartada.\n",
                    i + 1, e.energiaPostImpacto, desplazamientoPostImpacto);
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
  Serial.println("\n# Wi-Care: Deteccion de caidas (v4.9 - fix retorcerse + reidentificacion)...");
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

    manejarPresencia(e, obj.presente, obj);
    if (!e.presenciaConfirmada) continue;

    if (obj.presente) {
      bool frameValido = !esSaltoImposible(e, obj);

      actualizarFiltro(e, obj);
      manejarInmovilidadYCaida(i, e, obj, imprimir, frameValido);

      if (imprimir) {
        Serial.printf("Obj %d -> X=%-6.0f mm Y=%-6.0f mm V=%-6.1f cm/s\n",
                      i + 1, e.xFiltrado, e.yFiltrado, e.vFiltrado);
      }
    }
  }
}