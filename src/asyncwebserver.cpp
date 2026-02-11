#include "asyncwebserver.h"
#include "messages.h"
#include "webhandler.h"

#ifdef ENABLE_SERVER

AsyncWebServer server(80);

namespace
{
const char *methodToString(uint8_t method)
{
  switch (method)
  {
  case HTTP_GET:
    return "GET";
  case HTTP_POST:
    return "POST";
  case HTTP_DELETE:
    return "DELETE";
  case HTTP_PUT:
    return "PUT";
  case HTTP_PATCH:
    return "PATCH";
  case HTTP_HEAD:
    return "HEAD";
  case HTTP_OPTIONS:
    return "OPTIONS";
  default:
    return "UNKNOWN";
  }
}

void logRequest(AsyncWebServerRequest *request, const char *routeTag)
{
  Serial.print("[HTTP] ");
  Serial.print(routeTag);
  Serial.print(" ");
  Serial.print(methodToString(static_cast<uint8_t>(request->method())));
  Serial.print(" ");
  Serial.print(request->url());
  Serial.print(" from ");
  Serial.println(request->client()->remoteIP().toString());
}
} // namespace

void initWebServer()
{
  Serial.println("Initializing web server...");

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers",
                                       "Accept, Content-Type, Authorization");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Credentials", "true");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    logRequest(request, "root");
    startGui(request);
  });
  server.on("/health", HTTP_GET, [](AsyncWebServerRequest *request) {
    logRequest(request, "health");
    request->send(200, "text/plain", "ok");
  });
  server.onNotFound(
      [](AsyncWebServerRequest *request) {
        logRequest(request, "not_found");
        request->send(404, "text/plain", "Page not found!");
      });

  // Route to handle
  // http://your-server/message?text=Hello&repeat=3&id=42&delay=30&graph=1,2,3,4&miny=0&maxy=15
  server.on("/api/message", HTTP_GET, [](AsyncWebServerRequest *request) {
    logRequest(request, "api_message");
    handleMessage(request);
  });
  server.on("/api/removemessage", HTTP_GET, [](AsyncWebServerRequest *request) {
    logRequest(request, "api_removemessage");
    handleMessageRemove(request);
  });

  server.on("/api/info", HTTP_GET, [](AsyncWebServerRequest *request) {
    logRequest(request, "api_info");
    handleGetInfo(request);
  });

  // Handle API request to set an active plugin by ID
  server.on("/api/plugin", HTTP_PATCH, [](AsyncWebServerRequest *request) {
    logRequest(request, "api_plugin");
    handleSetPlugin(request);
  });

  // Handle API request to set the brightness (0..255);
  server.on("/api/brightness", HTTP_PATCH, [](AsyncWebServerRequest *request) {
    logRequest(request, "api_brightness");
    handleSetBrightness(request);
  });
  server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest *request) {
    logRequest(request, "api_data");
    handleGetData(request);
  });

  // Scheduler
  server.on("/api/schedule", HTTP_POST, [](AsyncWebServerRequest *request) {
    logRequest(request, "api_schedule");
    handleSetSchedule(request);
  });
  server.on("/api/schedule/clear", HTTP_GET, [](AsyncWebServerRequest *request) {
    logRequest(request, "api_schedule_clear");
    handleClearSchedule(request);
  });
  server.on("/api/schedule/stop", HTTP_GET, [](AsyncWebServerRequest *request) {
    logRequest(request, "api_schedule_stop");
    handleStopSchedule(request);
  });
  server.on("/api/schedule/start", HTTP_GET, [](AsyncWebServerRequest *request) {
    logRequest(request, "api_schedule_start");
    handleStartSchedule(request);
  });

  server.on("/api/storage/clear", HTTP_GET, [](AsyncWebServerRequest *request) {
    logRequest(request, "api_storage_clear");
    handleClearStorage(request);
  });

  server.begin();
  Serial.println("Web server started on port 80");
}

#endif
