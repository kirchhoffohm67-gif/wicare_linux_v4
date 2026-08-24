
#include "FirebaseSync.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>

void FirebaseSync::enviarEstado(float x, float y, bool alerta, const String &estadoTexto) {
  if (WiFi.status() != WL_CONNECTED) return;

  String url = "https://" + String(FIREBASE_HOST) + "/estado.json";

  String json = "{\"x\":" + String(x, 0) +
                ",\"y\":" + String(y, 0) +
                ",\"alerta\":" + (alerta ? "true" : "false") +
                ",\"texto\":\"" + estadoTexto + "\"" +
                ",\"timestamp\":" + String(millis()) + "}";

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.PUT(json);
  http.end();
}

