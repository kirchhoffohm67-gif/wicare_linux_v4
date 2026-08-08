#include "RadarLD2450.h"

void RadarLD2450::begin(HardwareSerial &serial) {
  _serial = &serial;
  _serial->begin(RADAR_BAUD, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);
}

int16_t RadarLD2450::decodificar(uint16_t raw) {
  if (raw & 0x8000) return (int16_t)(raw & 0x7FFF);
  return -(int16_t)raw;
}

void RadarLD2450::update() {
  while (_serial->available()) {
    procesarByte((uint8_t)_serial->read());
  }
}

void RadarLD2450::procesarByte(uint8_t b) {
  if (_idx < 4) {
    if (b == FRAME_HDR[_idx]) {
      _buf[_idx++] = b;
    } else {
      // Reinicia búsqueda de cabecera; el byte actual puede ser
      // el inicio de una nueva cabecera (cubre el caso HDR[0]==HDR[2], etc).
      _idx = (b == FRAME_HDR[0]) ? 1 : 0;
      if (_idx) _buf[0] = FRAME_HDR[0];
    }
    return;
  }

  _buf[_idx++] = b;

  if (_idx == FRAME_LEN) {
    if (_buf[28] == FRAME_TAIL[0] && _buf[29] == FRAME_TAIL[1]) {
      parsearFrame();
      _ultimoFrameValido = millis();
      _frameNuevo = true;
    }
    _idx = 0;
  }
}

void RadarLD2450::parsearFrame() {
  for (int i = 0; i < MAX_OBJETIVOS; i++) {
    int offset = 4 + (i * 8);
    uint16_t rawX = _buf[offset]     | (_buf[offset + 1] << 8);
    uint16_t rawY = _buf[offset + 2] | (_buf[offset + 3] << 8);
    uint16_t rawV = _buf[offset + 4] | (_buf[offset + 5] << 8);

    _objetivos[i].x = decodificar(rawX);
    _objetivos[i].y = decodificar(rawY);
    _objetivos[i].velocidad = decodificar(rawV);
    _objetivos[i].presente = (_objetivos[i].x != 0 || _objetivos[i].y != 0);
  }
}