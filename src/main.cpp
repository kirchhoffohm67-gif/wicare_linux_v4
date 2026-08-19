#include <Arduino.h>
#include "config.h"
#include "RadarLD2450.h"
#include "NtfyNotifier.h"
#include "esp_task_wdt.h"

HardwareSerial RadarSerial(1);
RadarLD2450 radar;
NtfyNotifier notificador;

unsigned long ultimaImpresion = 0;
bool radarEstabaActivo = true;

#define SALTO_MAXIMO_MM 600
#define FRAMES_PARA_CANCELAR_QUIETO 3

struct EstadoObjetivo {
  float xFiltrado = 0, yFiltrado = 0, vFiltrado = 0;
  bool filtroInicializado = false;

  float xAnteriorCalc = 0, yAnteriorCalc = 0;
  unsigned long tiempoAnteriorCalc = 0;
  bool calcInicializado = false;
  float velocidadCalculada = 0;

  int16_t velocidadAnteriorRaw = 0;
  uint8_t framesVelocidadRepetida = 0;

  uint8_t framesPresente = 0;
  uint8_t framesAusente = 0;
  bool presenciaConfirmada = false;
  unsigned long tiempoUltimaDeteccion = 0;

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

  bool esperandoDesaceleracion = false;
  unsigned long inicioEsperaDesaceleracion = 0;
  uint8_t framesDesaceleracionSeguidos = 0;

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

void verificarRegistrosPerdidosVencidos() {
  for (int j = 0; j < MAX_OBJETIVOS; j++) {
    RegistroPerdido &r = registrosPerdidos[j];
    if (!r.activo) continue;
    if (millis() - r.tiempoPerdida > VENTANA_REIDENTIFICACION_MS) {
      if (r.estabaEnConfirmacion) {
        Serial.printf(">>> ALERTA CAIDA (por desaparicion): objetivo desaparecio durante confirmacion "
                      "de posible caida y no volvio a detectarse. Ultima pos conocida: X=%.0f Y=%.0f. "
                      "Ultimo burst: X=%d Y=%d V=%d cm/s. <<<\n",
                      r.x, r.y, r.xUltimoBurst, r.yUltimoBurst, r.velocidadUltimoBurst);
        notificador.enviarAlerta("El sensor perdio a la persona durante una posible caida. Requiere revision urgente.");
      }
      r.activo = false;
    }
  }
}

void resetEstado(EstadoObjetivo &e) {
  guardarRegistroPerdido(e);
  e = EstadoObjetivo();
}

void verificarTimeoutForzado(EstadoObjetivo &e, int i) {
  if (!e.filtroInicializado) return;
  if (e.tiempoUltimaDeteccion == 0) return;
  if (millis() - e.tiempoUltimaDeteccion > TIEMPO_MAXIMO_SIN_DETECCION_MS) {
    Serial.printf("[INFO] Obj %d - liberado por timeout forzado (sin deteccion real hace %.1fs)\n",
                  i + 1, (millis() - e.tiempoUltimaDeteccion) / 1000.0);
    resetEstado(e);
  }
}

bool esSaltoImposible(const EstadoObjetivo &e, const Objetivo &obj) {
  if (!e.filtroInicializado) return false;
  float dx = obj.x - e.xFiltrado;
  float dy = obj.y - e.yFiltrado;
  return sqrtf(dx * dx + dy * dy) > SALTO_MAXIMO_MM;
}

bool esVelocidadSospechosamenteConstante(EstadoObjetivo &e, int16_t velocidadActual) {
  bool esAlta = abs(velocidadActual) >= 100;
  bool igualQueAntes = (velocidadActual == e.velocidadAnteriorRaw);

  if (esAlta && igualQueAntes) {
    e.framesVelocidadRepetida++;
  } else {
    e.framesVelocidadRepetida = 0;
  }
  e.velocidadAnteriorRaw = velocidadActual;

  return e.framesVelocidadRepetida >= 2;
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

void actualizarVelocidadCalculada(EstadoObjetivo &e) {
  unsigned long ahora = millis();
  if (!e.calcInicializado) {
    e.xAnteriorCalc = e.xFiltrado;
    e.yAnteriorCalc = e.yFiltrado;
    e.tiempoAnteriorCalc = ahora;
    e.calcInicializado = true;
    e.velocidadCalculada = 0;
    return;
  }
  unsigned long dt = ahora - e.tiempoAnteriorCalc;
  if (dt < MIN_DT_CALCULO_MS) return;

  float dx = e.xFiltrado - e.xAnteriorCalc;
  float dy = e.yFiltrado - e.yAnteriorCalc;
  float distMm = sqrtf(dx * dx + dy * dy);
  e.velocidadCalculada = (distMm / dt) * 100.0f;

  e.xAnteriorCalc = e.xFiltrado;
  e.yAnteriorCalc = e.yFiltrado;
  e.tiempoAnteriorCalc = ahora;
}

void manejarPresencia(EstadoObjetivo &e, bool rawPresente, const Objetivo &obj) {
  if (rawPresente) {
    e.tiempoUltimaDeteccion = millis();
    e.framesPresente++;
    e.framesAusente = 0;
    if (!e.presenciaConfirmada && e.framesPresente >= PRESENCE_CONFIRM_FRAMES) {
      e.presenciaConfirmada = true;
      intentarReidentificar(e, obj.x, obj.y);
    }
  } else {
    e.framesAusente++;
    e.framesPresente = 0;
    if (e.framesAusente == 1 && e.presenciaConfirmada) {
      Serial.printf("[INFO] Objetivo dejo de detectarse%s (ultima pos X=%.0f Y=%.0f)\n",
                    e.enConfirmacionCaida ? " MIENTRAS CONFIRMABA POSIBLE CAIDA" : "",
                    e.xFiltrado, e.yFiltrado);
    }
    if (e.presenciaConfirmada && e.framesAusente >= PRESENCE_LOST_FRAMES) {
      resetEstado(e);
    }
  }
}

void manejarInmovilidadYCaida(int i, EstadoObjetivo &e, const Objetivo &obj, bool imprimir, bool frameValido) {
  float velocidadEfectiva = fmaxf(fabs(obj.velocidad), e.velocidadCalculada);

  bool esteFrameBrusco = frameValido && (velocidadEfectiva >= VELOCIDAD_MOVIMIENTO_BRUSCO) &&
                         (fabs(obj.velocidad) >= VELOCIDAD_DOPPLER_MINIMA_RESPALDO);
  e.framesBrusco = esteFrameBrusco ? (e.framesBrusco + 1) : 0;

  bool burstPorFramesConsecutivos = e.framesBrusco >= FRAMES_BRUSCO_CONFIRMAR;
  bool burstInstantaneo = frameValido && (fabs(obj.velocidad) >= VELOCIDAD_BURST_INSTANTANEO);

  bool nuevoBurstConfirmado = false;
  if (burstPorFramesConsecutivos || burstInstantaneo) {
    if (e.ultimoMovimientoBrusco == 0 || millis() - e.ultimoMovimientoBrusco > 200) {
      Serial.printf("[BURST] Obj %d - movimiento brusco confirmado (vDoppler=%d vCalc=%.0f cm/s, %s)\n",
                    i + 1, obj.velocidad, e.velocidadCalculada, burstInstantaneo ? "instantaneo" : "sostenido");
      nuevoBurstConfirmado = true;
    }
    e.ultimoMovimientoBrusco = millis();
    e.velocidadUltimoBurst = obj.velocidad;
    e.xUltimoBurst = obj.x;
    e.yUltimoBurst = obj.y;
  }

  bool enCooldown = (millis() - e.ultimaAlertaCaida) < COOLDOWN_ALERTA_MS;

  if (nuevoBurstConfirmado && !e.enConfirmacionCaida && !e.esperandoDesaceleracion && !enCooldown) {
    e.esperandoDesaceleracion = true;
    e.inicioEsperaDesaceleracion = millis();
    e.framesDesaceleracionSeguidos = 0;
    Serial.printf("[INFO] Obj %d - pico detectado, esperando desaceleracion para confirmar impacto...\n", i + 1);
  }

  if (e.esperandoDesaceleracion) {
    // Solo Doppler decide si ya "se detuvo" el impacto - vCalc tiene inercia
    // de su ventana de calculo y queda inflado varios frames despues de un
    // impacto real, haciendo que la caida parezca "seguir en movimiento".
    bool desacelero = frameValido && fabs(obj.velocidad) <= DESACELERACION_UMBRAL_CM_S;
    e.framesDesaceleracionSeguidos = desacelero ? (e.framesDesaceleracionSeguidos + 1) : 0;
    unsigned long tEspera = millis() - e.inicioEsperaDesaceleracion;

    if (e.framesDesaceleracionSeguidos >= FRAMES_DESACELERACION_CONFIRMAR) {
      e.esperandoDesaceleracion = false;
      e.enConfirmacionCaida = true;
      e.inicioConfirmacionCaida = millis();
      e.framesRecuperacion = 0;
      e.baseRecuperacionCapturada = false;
      e.energiaPostImpacto = 0;
      Serial.printf("[INFO] Obj %d - desaceleracion confirmada (impacto real) - iniciando confirmacion de posible caida (%.0fs de ventana)...\n",
                    i + 1, TIEMPO_ESPERA_ALERTA_MS / 1000.0);
    } else if (tEspera > VENTANA_ESPERA_DESACELERACION_MS) {
      e.esperandoDesaceleracion = false;
      Serial.printf("[INFO] Obj %d - no hubo desaceleracion tras el pico (sigue en movimiento) - probablemente caminando, no caida.\n", i + 1);
    }
  }

  if (e.enConfirmacionCaida) {
    unsigned long tiempoEnConfirmacion = millis() - e.inicioConfirmacionCaida;
    bool dentroDeMargenDeCaida = tiempoEnConfirmacion < TIEMPO_MINIMO_ANTES_RECUPERACION_MS;

    if (!dentroDeMargenDeCaida && !e.baseRecuperacionCapturada) {
      e.xBaseRecuperacion = e.xFiltrado;
      e.yBaseRecuperacion = e.yFiltrado;
      e.baseRecuperacionCapturada = true;
    }

    bool movimientoDeRecuperacion = !dentroDeMargenDeCaida && frameValido &&
                                     (velocidadEfectiva >= VELOCIDAD_RECUPERACION_CM_S);
    e.framesRecuperacion = movimientoDeRecuperacion ? (e.framesRecuperacion + 1) : 0;

    if (!dentroDeMargenDeCaida && frameValido) {
      e.energiaPostImpacto += velocidadEfectiva;
    }

    float desplazamientoPostImpacto = 0;
    if (e.baseRecuperacionCapturada) {
      desplazamientoPostImpacto = sqrtf(powf(e.xFiltrado - e.xBaseRecuperacion, 2) +
                                         powf(e.yFiltrado - e.yBaseRecuperacion, 2));
    }

    bool cancelaPorDesplazamientoAlto = e.baseRecuperacionCapturada &&
                                         desplazamientoPostImpacto > DESPLAZAMIENTO_SOLO_ALTO_MM;

    bool movimientoSostenido = (e.energiaPostImpacto > ENERGIA_RECUPERACION_UMBRAL &&
                                 e.baseRecuperacionCapturada &&
                                 desplazamientoPostImpacto > DESPLAZAMIENTO_CANCELA_CONFIRMACION_MM) ||
                                cancelaPorDesplazamientoAlto;

    Serial.printf("   [debug-caida] Obj %d confirmando=%.1fs framesRecuperacion=%d vDoppler=%d vCalc=%.0f valido=%d energia=%.0f desplazPostImpacto=%.0fmm\n",
                  i + 1, tiempoEnConfirmacion / 1000.0, e.framesRecuperacion, obj.velocidad, e.velocidadCalculada,
                  frameValido, e.energiaPostImpacto, desplazamientoPostImpacto);

    if (movimientoSostenido) {
      Serial.printf("[INFO] Obj %d se movio/recupero tras asentarse (energia=%.0f, desplazamiento=%.0fmm) - caida descartada.\n",
                    i + 1, e.energiaPostImpacto, desplazamientoPostImpacto);
      e.enConfirmacionCaida = false;
    } else if (tiempoEnConfirmacion >= TIEMPO_ESPERA_ALERTA_MS) {
      Serial.printf(">>> ALERTA CAIDA: Obj %d - inmovil %.1fs. Frame que disparo: X=%d Y=%d V=%d cm/s. Posicion actual: X=%.0f Y=%.0f. <<<\n",
                    i + 1, tiempoEnConfirmacion / 1000.0,
                    e.xUltimoBurst, e.yUltimoBurst, e.velocidadUltimoBurst,
                    e.xFiltrado, e.yFiltrado);
      notificador.enviarAlerta("Posible caida detectada. Persona inmovil por " +
                                String(tiempoEnConfirmacion / 1000.0, 1) + "s.");
      e.enConfirmacionCaida = false;
      e.ultimaAlertaCaida = millis();
    }
  }

  bool porEncimaUmbral = velocidadEfectiva > UMBRAL_VELOCIDAD_QUIETO;
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
    Serial.printf("   [debug] Obj %d quieto=%d framesMov=%d vDoppler=%d vCalc=%.0f valido=%d esperandoDecel=%d confirmandoCaida=%d\n",
                  i + 1, e.temporizadorActivo, e.framesMovimiento, obj.velocidad, e.velocidadCalculada,
                  frameValido, e.esperandoDesaceleracion, e.enConfirmacionCaida);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n# Wi-Care: Deteccion de caidas (v6.3 - desaceleracion solo por Doppler)...");
  radar.begin(RadarSerial);
  notificador.begin();

  esp_task_wdt_init(WDT_TIMEOUT_SEGUNDOS, true);
  esp_task_wdt_add(NULL);
  Serial.println("[INFO] Watchdog activado - reinicio automatico si el sistema se cuelga.");
}

void loop() {
  esp_task_wdt_reset();
  radar.update();
  verificarRegistrosPerdidosVencidos();

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
    verificarTimeoutForzado(estado[i], i);
  }

  Objetivo raw[MAX_OBJETIVOS];
  bool rawClaimed[MAX_OBJETIVOS];
  for (int j = 0; j < MAX_OBJETIVOS; j++) {
    raw[j] = radar.objetivo(j);
    rawClaimed[j] = false;
  }

  int matchDeIdentidad[MAX_OBJETIVOS];
  for (int i = 0; i < MAX_OBJETIVOS; i++) {
    matchDeIdentidad[i] = -1;
    EstadoObjetivo &e = estado[i];
    if (!e.filtroInicializado) continue;

    int mejorJ = -1;
    float mejorDist = 1e9;
    for (int j = 0; j < MAX_OBJETIVOS; j++) {
      if (rawClaimed[j] || !raw[j].presente) continue;
      float dx = raw[j].x - e.xFiltrado;
      float dy = raw[j].y - e.yFiltrado;
      float dist = sqrtf(dx * dx + dy * dy);
      if (dist < mejorDist) { mejorDist = dist; mejorJ = j; }
    }
    float radioMatch = e.enConfirmacionCaida ? MATCH_MAX_DIST_DURANTE_CAIDA_MM : MATCH_MAX_DIST_MM;
    if (mejorJ != -1 && mejorDist <= radioMatch) {
      rawClaimed[mejorJ] = true;
      matchDeIdentidad[i] = mejorJ;
    }
  }

  for (int j = 0; j < MAX_OBJETIVOS; j++) {
    if (rawClaimed[j] || !raw[j].presente) continue;
    for (int i = 0; i < MAX_OBJETIVOS; i++) {
      if (!estado[i].filtroInicializado && matchDeIdentidad[i] == -1) {
        rawClaimed[j] = true;
        matchDeIdentidad[i] = j;
        break;
      }
    }
  }

  for (int i = 0; i < MAX_OBJETIVOS; i++) {
    EstadoObjetivo &e = estado[i];
    bool detectadoEsteFrame = matchDeIdentidad[i] != -1;
    Objetivo objUsar = detectadoEsteFrame ? raw[matchDeIdentidad[i]] : Objetivo();

    manejarPresencia(e, detectadoEsteFrame, objUsar);
    if (!e.presenciaConfirmada) continue;

    if (detectadoEsteFrame) {
      bool frameValido = !esSaltoImposible(e, objUsar) &&
                          (fabs(objUsar.velocidad) <= VELOCIDAD_MAXIMA_HUMANA_CM_S) &&
                          !esVelocidadSospechosamenteConstante(e, objUsar.velocidad);

      actualizarFiltro(e, objUsar);
      if (frameValido) {
        actualizarVelocidadCalculada(e);
      }
      manejarInmovilidadYCaida(i, e, objUsar, imprimir, frameValido);

      if (imprimir) {
        Serial.printf("Obj %d -> X=%-6.0f mm Y=%-6.0f mm V=%-6.1f cm/s\n",
                      i + 1, e.xFiltrado, e.yFiltrado, e.vFiltrado);
      }
    }
  }
}
