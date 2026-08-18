#pragma once
#include <Arduino.h>

class WhatsAppNotifier {
public:
  void begin();
  void enviarAlerta(const String &mensaje);
  bool wifiConectado();

private:
  unsigned long ultimoIntentoWifi = 0;
  String urlEncode(const String &texto);
};