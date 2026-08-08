#pragma once
#include <Arduino.h>
#include "config.h"

struct Objetivo {
  int16_t x = 0;
  int16_t y = 0;
  int16_t velocidad = 0;
  bool presente = false;
};

class RadarLD2450 {
public:
  void begin(HardwareSerial &serial);
  void update();  // llamar en cada loop(); procesa bytes disponibles

  const Objetivo& objetivo(int i) const { return _objetivos[i]; }
  bool frameNuevoDisponible() const { return _frameNuevo; }
  void limpiarFlagFrameNuevo() { _frameNuevo = false; }

  bool radarActivo() const { return (millis() - _ultimoFrameValido) < RADAR_TIMEOUT_MS; }

private:
  HardwareSerial* _serial = nullptr;
  uint8_t _buf[FRAME_LEN];
  uint8_t _idx = 0;
  Objetivo _objetivos[MAX_OBJETIVOS];
  bool _frameNuevo = false;
  unsigned long _ultimoFrameValido = 0;

  static int16_t decodificar(uint16_t raw);
  void procesarByte(uint8_t b);
  void parsearFrame();
};
