#include "WhatsAppNotifier.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>

void WhatsAppNotifier::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WIFI] Conectando a %s", WIFI_SSID);

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 15000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WIFI] Conectado. IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[WIFI] No se pudo conectar (se reintentara mas adelante).");
  }
}

bool WhatsAppNotifier::wifiConectado() {
  return WiFi.status() == WL_CONNECTED;
}

// Codificacion de URL manual (sin depender de una libreria externa) -
// reemplaza espacios y caracteres especiales por su forma %XX.
String WhatsAppNotifier::urlEncode(const String &texto) {
  String salida = "";
  char c;
  char code[4];
  for (unsigned int i = 0; i < texto.length(); i++) {
    c = texto.charAt(i);
    if (isalnum(c)) {
      salida += c;
    } else if (c == ' ') {
      salida += "%20";
    } else {
      sprintf(code, "%%%02X", c);
      salida += code;
    }
  }
  return salida;
}

void WhatsAppNotifier::enviarAlerta(const String &mensaje) {
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - ultimoIntentoWifi > WIFI_RECONEXION_INTERVALO_MS) {
      ultimoIntentoWifi = millis();
      Serial.println("[WIFI] Reintentando conexion...");
      WiFi.reconnect();
    }
    Serial.println("[WHATSAPP] No se pudo enviar - sin conexion WiFi.");
    return;
  }

  String mensajeCodificado = urlEncode(mensaje);
  String url = "https://api.callmebot.com/whatsapp.php?phone=" + String(WHATSAPP_TELEFONO) +
               "&text=" + mensajeCodificado + "&apikey=" + String(WHATSAPP_APIKEY);

  HTTPClient http;
  http.begin(url);
  int codigoRespuesta = http.GET();

  if (codigoRespuesta > 0) {
    Serial.printf("[WHATSAPP] Enviado. Codigo de respuesta: %d\n", codigoRespuesta);
  } else {
    Serial.printf("[WHATSAPP] Fallo al enviar. Error: %s\n", http.errorToString(codigoRespuesta).c_str());
  }
  http.end();
}