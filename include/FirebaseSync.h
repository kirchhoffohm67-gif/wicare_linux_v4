
#pragma once
#include <Arduino.h>

class FirebaseSync {
public:
  void enviarEstado(float x, float y, bool alerta, const String &estadoTexto);
};
