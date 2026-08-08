#pragma once

// ---- Radar LD2450 (UART) ----
#define RADAR_RX_PIN 18
#define RADAR_TX_PIN 17
#define RADAR_BAUD   256000

// ---- Protocolo del frame ----
#define FRAME_LEN 30
static const uint8_t FRAME_HDR[4] = {0xAA, 0xFF, 0x03, 0x00};
static const uint8_t FRAME_TAIL[2] = {0x55, 0xCC};

// ---- Lógica de inmovilidad ----
#define MAX_OBJETIVOS 3
#define UMBRAL_VELOCIDAD_QUIETO 5     // cm/s
#define TIEMPO_ESPERA_ALERTA_MS 3000  // ms inmóvil antes de alertar
#define COOLDOWN_ALERTA_MS 10000      // ms antes de poder re-alertar el mismo objetivo

// ---- Salud del sistema ----
#define RADAR_TIMEOUT_MS 2000  // si no llega ningún frame válido en este tiempo -> radar "caído"

// ---- Debug ----
#define INTERVALO_IMPRESION_MS 300