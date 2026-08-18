#include <Arduino.h>
#include "config.h"
#include "WhatsAppNotifier.h"

WhatsAppNotifier whatsapp;

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n# Prueba de WiFi + WhatsApp (CallMeBot)...");
  whatsapp.begin();

  if (whatsapp.wifiConectado()) {
    whatsapp.enviarAlerta("Prueba desde Wi-Care: si recibes esto, la conexion funciona correctamente.");
  }
}

void loop() {
  // nada por ahora, solo probamos el envio una vez en setup()
}