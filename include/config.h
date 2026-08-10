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
#define PRESENCE_CONFIRM_FRAMES 3   // frames seguidos para confirmar que SI hay alguien
#define PRESENCE_LOST_FRAMES 5      // frames seguidos para confirmar que YA NO hay nadie

// ---- Filtro de suavizado (EMA) ----
#define FILTRO_ALPHA 0.3f  // 0 = muy suave/lento, 1 = sin filtro

// ---- Inmovilidad / caidas ----
#define UMBRAL_VELOCIDAD_QUIETO 5        // cm/s, tolerancia de "quieto"
#define VELOCIDAD_MOVIMIENTO_BRUSCO 30   // cm/s, se considera movimiento fuerte
#define VENTANA_CAIDA_MS 1500            // ms entre movimiento brusco y parada para sospechar caida
#define TIEMPO_ESPERA_ALERTA_MS 3000     // ms quieto tras movimiento brusco -> ALERTA CAIDA
#define TIEMPO_INACTIVIDAD_LARGA_MS 60000 // ms quieto SIN movimiento brusco previo -> aviso de inactividad
#define DESPLAZAMIENTO_CANCELA_QUIETO_MM 150
#define FRAMES_RECUPERACION_TRAS_CAIDA 5
#define VELOCIDAD_RECUPERACION_CM_S 15

// ---- Debug ----
#define INTERVALO_IMPRESION_MS 300

#define FRAMES_BRUSCO_CONFIRMAR 2    // frames RAW consecutivos con velocidad alta para confirmar (no un solo frame de ruido)
#define COOLDOWN_ALERTA_MS 10000     // ms de espera minima antes de poder disparar OTRA alerta de caida tras la anterior

#define TIEMPO_MINIMO_ANTES_RECUPERACION_MS 800