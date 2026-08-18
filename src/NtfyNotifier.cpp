#include "NtfyNotifier.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>

void NtfyNotifier::begin() {
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

bool NtfyNotifier::wifiConectado() {
  return WiFi.status() == WL_CONNECTED;
}

void NtfyNotifier::enviarAlerta(const String &mensaje) {
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - ultimoIntentoWifi > WIFI_RECONEXION_INTERVALO_MS) {
      ultimoIntentoWifi = millis();
      Serial.println("[WIFI] Reintentando conexion...");
      WiFi.reconnect();
    }
    Serial.println("[NTFY] No se pudo enviar - sin conexion WiFi.");
    return;
  }

  String url = "https://ntfy.sh/" + String(NTFY_TOPIC);

  HTTPClient http;
  http.begin(url);
  http.addHeader("Title", "Wi-Care - Alerta de Caida");
  http.addHeader("Priority", "urgent");
  http.addHeader("Tags", "rotating_light");

  int codigoRespuesta = http.POST(mensaje);

  if (codigoRespuesta > 0) {
    Serial.printf("[NTFY] Enviado. Codigo de respuesta: %d\n", codigoRespuesta);
  } else {
    Serial.printf("[NTFY] Fallo al enviar. Error: %s\n", http.errorToString(codigoRespuesta).c_str());
  }
  http.end();
}