#pragma once

// ---- Radar LD2450 (UART) ----
#define RADAR_RX_PIN 18
#define RADAR_TX_PIN 17
#define RADAR_BAUD   256000

// ---- Protocolo del frame ----
#define FRAME_LEN 30
static const uint8_t FRAME_HDR[4] = {0xAA, 0xFF, 0x03, 0x00};
static const uint8_t FRAME_TAIL[2] = {0x55, 0xCC};

// ---- Salud del sistema ----
#define RADAR_TIMEOUT_MS 2000
#define MAX_OBJETIVOS 3

// ---- Debounce de presencia (filtra fantasmas y parpadeo entre 1-2 personas) ----
#define PRESENCE_CONFIRM_FRAMES 7   // subido de 5 - mas resistencia a fantasmas con gente moviendose cerca
#define PRESENCE_LOST_FRAMES 5

// ---- Filtro de suavizado (EMA) ----
#define FILTRO_ALPHA 0.3f

// ---- Inmovilidad / caidas ----
#define UMBRAL_VELOCIDAD_QUIETO 5
#define VELOCIDAD_MOVIMIENTO_BRUSCO 40
#define VENTANA_CAIDA_MS 1500
#define TIEMPO_ESPERA_ALERTA_MS 4000
#define TIEMPO_INACTIVIDAD_LARGA_MS 60000
#define DESPLAZAMIENTO_CANCELA_QUIETO_MM 150
#define FRAMES_RECUPERACION_TRAS_CAIDA 3
#define VELOCIDAD_RECUPERACION_CM_S 15

// ---- Debug ----
#define INTERVALO_IMPRESION_MS 300

#define FRAMES_BRUSCO_CONFIRMAR 4   // subido de 3 - mas exigente con ruido de ambiente concurrido
#define COOLDOWN_ALERTA_MS 10000

#define TIEMPO_MINIMO_ANTES_RECUPERACION_MS 1000
#define DESPLAZAMIENTO_CANCELA_CONFIRMACION_MM 80   // punto intermedio - ni 40 (muy laxo) ni 150 (muy estricto)
#define ENERGIA_RECUPERACION_UMBRAL 80.0f

#define VELOCIDAD_BURST_INSTANTANEO 110

#define VENTANA_REIDENTIFICACION_MS 800
#define DISTANCIA_REIDENTIFICACION_MM 300   // bajado de 500 - reduce riesgo de reidentificar con otra persona

#define VELOCIDAD_MAXIMA_HUMANA_CM_S 230   // bajado de 260 - mallas metalicas de cancha generan mas multipath
#define MATCH_MAX_DIST_MM 450
#define MATCH_MAX_DIST_DURANTE_CAIDA_MM 300   // punto intermedio - ni 200 (te pierde) ni 500 (engancha a otros)

#define VENTANA_ESPERA_DESACELERACION_MS 1300
#define DESACELERACION_UMBRAL_CM_S 12

#define MIN_DT_CALCULO_MS 250
#define DESPLAZAMIENTO_SOLO_ALTO_MM 350

#define TIEMPO_MAXIMO_SIN_DETECCION_MS 10000

// ---- WiFi y notificaciones (ntfy.sh) ----
// IMPORTANTE: cambiar SSID/PASSWORD a la red del colegio/lugar de competencia antes de salir de casa
#define WIFI_SSID "Sanchez Freitez 2.4"
#define WIFI_PASSWORD "Vf17627580"
#define NTFY_TOPIC "Wicare-linux-V4-"
#define WIFI_RECONEXION_INTERVALO_MS 10000

// ---- Watchdog ----
#define WDT_TIMEOUT_SEGUNDOS 15

#define FRAMES_DESACELERACION_CONFIRMAR 3
#define VELOCIDAD_DOPPLER_MINIMA_RESPALDO 20