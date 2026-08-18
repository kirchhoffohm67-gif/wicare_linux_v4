#pragma once
#include <Arduino.h>

class NtfyNotifier {
public:
  void begin();
  void enviarAlerta(const String &mensaje);
  bool wifiConectado();

private:
  unsigned long ultimoIntentoWifi = 0;
};