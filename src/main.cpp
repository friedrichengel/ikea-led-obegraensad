#include <Arduino.h>
#include <BfButton.h>
#include <SPI.h>

#include <ETH.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "PluginManager.h"
#include "scheduler.h"

#include "plugins/ArtNet.h"
#include "plugins/Blob.h"
#include "plugins/BreakoutPlugin.h"
#include "plugins/CirclePlugin.h"
#include "plugins/DDPPlugin.h"
#include "plugins/DrawPlugin.h"
#include "plugins/FireworkPlugin.h"
#include "plugins/GameOfLifePlugin.h"
#include "plugins/LinesPlugin.h"
#include "plugins/PongClockPlugin.h"
#include "plugins/RainPlugin.h"
#include "plugins/SnakePlugin.h"
#include "plugins/StarsPlugin.h"
#include "plugins/TickingClockPlugin.h"

#ifdef ENABLE_SERVER
#include "plugins/AnimationPlugin.h"
#include "plugins/BigClockPlugin.h"
#include "plugins/ClockPlugin.h"
#include "plugins/WeatherPlugin.h"
#endif

#include "asyncwebserver.h"
#include "messages.h"
#include "ota.h"
#include "screen.h"
#include "secrets.h"
#include "storage.h"
#include "websocket.h"

BfButton btn(BfButton::STANDALONE_DIGITAL, PIN_BUTTON, true, LOW);

unsigned long previousMillis = 0;
unsigned long interval = 30000;

PluginManager pluginManager;
DRAM_ATTR volatile SYSTEM_STATUS currentStatus = NONE;

unsigned long lastConnectionAttempt = 0;
const unsigned long connectionInterval = 10000;
unsigned long reconnectionBackoff = 5000;            // Start with 5 seconds
const unsigned long maxReconnectionBackoff = 300000; // Max 5 minutes
uint8_t reconnectionAttempts = 0;
unsigned long dhcpStartMillis = 0;
const unsigned long dhcpTimeoutMs = 15000;
bool staticFallbackApplied = false;

static bool mdnsStarted = false;

void logEthConfig()
{
#if defined(ETH_PHY_TYPE)
  Serial.print("ETH PHY type: ");
  Serial.println(ETH_PHY_TYPE);
#endif
#if defined(ETH_PHY_ADDR)
  Serial.print("ETH PHY addr: ");
  Serial.println(ETH_PHY_ADDR);
#endif
#if defined(ETH_PHY_MDC)
  Serial.print("ETH MDC: ");
  Serial.println(ETH_PHY_MDC);
#endif
#if defined(ETH_PHY_MDIO)
  Serial.print("ETH MDIO: ");
  Serial.println(ETH_PHY_MDIO);
#endif
#if defined(ETH_PHY_POWER)
  Serial.print("ETH PWR pin: ");
  Serial.println(ETH_PHY_POWER);
#endif
#if defined(ETH_CLK_MODE)
  Serial.print("ETH clock mode: ");
  Serial.println(ETH_CLK_MODE);
#endif
}

void logEthStatus()
{
  Serial.print("ETH started: ");
  Serial.print(ETH.started() ? "yes" : "no");
  Serial.print(" link: ");
  Serial.print(ETH.linkUp() ? "up" : "down");
  Serial.print(" hasIP: ");
  Serial.println(ETH.hasIP() ? "yes" : "no");
}

void NetworkEvent(arduino_event_id_t event)
{
  switch (event)
  {
  case ARDUINO_EVENT_ETH_START:
    Serial.println("Ethernet started");
    break;
  case ARDUINO_EVENT_ETH_CONNECTED:
    Serial.println("Ethernet connected");
    break;
  case ARDUINO_EVENT_ETH_GOT_IP:
    Serial.print("Ethernet IPv4: ");
    Serial.println(ETH.localIP());
    Serial.print("Gateway: ");
    Serial.println(ETH.gatewayIP());
    Serial.print("Subnet: ");
    Serial.println(ETH.subnetMask());
    Serial.print("DNS: ");
    Serial.println(ETH.dnsIP());
    if (!mdnsStarted)
    {
      if (MDNS.begin(WIFI_HOSTNAME))
      {
        MDNS.addService("http", "tcp", 80);
        MDNS.setInstanceName(WIFI_HOSTNAME);
        mdnsStarted = true;
      }
      else
      {
        Serial.println("Could not start mDNS!");
      }
    }
    break;
  case ARDUINO_EVENT_ETH_GOT_IP6:
#if CONFIG_LWIP_IPV6
    if (ETH.hasGlobalIPv6())
    {
      Serial.print("Ethernet IPv6: ");
      Serial.println(ETH.globalIPv6());
    }
    else if (ETH.hasLinkLocalIPv6())
    {
      Serial.print("Ethernet IPv6 (LL): ");
      Serial.println(ETH.linkLocalIPv6());
    }
#endif
    break;
  case ARDUINO_EVENT_ETH_LOST_IP:
    Serial.println("Ethernet lost IPv4");
    break;
  case ARDUINO_EVENT_ETH_DISCONNECTED:
    Serial.println("Ethernet disconnected");
    break;
  case ARDUINO_EVENT_ETH_STOP:
    Serial.println("Ethernet stopped");
    break;
  default:
    Serial.print("Ethernet event: ");
    Serial.println(event);
    break;
  }
}

void connectToEthernet()
{
  Network.onEvent(NetworkEvent);

  logEthConfig();

  if (ETH.started() || ETH.linkUp() || ETH.hasIP())
  {
    Serial.println("ETH already started, skipping begin");
    return;
  }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
  Serial.println("Resetting Ethernet before reconnect...");
  ETH.end();
  delay(50);
#endif

#if defined(ETH_PHY_TYPE) && defined(ETH_PHY_ADDR) && defined(ETH_PHY_MDC) && defined(ETH_PHY_MDIO) && defined(ETH_PHY_POWER) && defined(ETH_CLK_MODE)
  if (!ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_POWER, ETH_CLK_MODE))
  {
    Serial.println("ETH.begin failed");
    lastConnectionAttempt = millis();
    return;
  }
#else
  if (!ETH.begin())
  {
    Serial.println("ETH.begin failed");
    lastConnectionAttempt = millis();
    return;
  }
#endif

  ETH.setHostname(WIFI_HOSTNAME);
  ETH.enableIPv6(true);

  Serial.println("ETH.begin OK, waiting for IP...");
  dhcpStartMillis = millis();
  staticFallbackApplied = false;

#if defined(IP_ADDRESS) && defined(GWY) && defined(SUBNET) && defined(DNS1)
  auto ip = IPAddress();
  ip.fromString(IP_ADDRESS);

  auto gwy = IPAddress();
  gwy.fromString(GWY);

  auto subnet = IPAddress();
  subnet.fromString(SUBNET);

  auto dns = IPAddress();
  dns.fromString(DNS1);

  ETH.config(ip, gwy, subnet, dns);
#endif

  lastConnectionAttempt = millis();
}

void pressHandler(BfButton *btn, BfButton::press_pattern_t pattern)
{
  switch (pattern)
  {
  case BfButton::SINGLE_PRESS:
    if (currentStatus != LOADING)
    {
      Scheduler.clearSchedule();
      pluginManager.activateNextPlugin();
    }
    break;

  case BfButton::LONG_PRESS:
    if (currentStatus != LOADING)
    {
      pluginManager.activatePersistedPlugin();
    }
    break;
  }
}

void baseSetup()
{
  Serial.begin(115200);
  logEthConfig();

#ifdef ENABLE_STORAGE
  // Ensure namespace exists before any read-only opens to avoid NOT_FOUND warnings
  storage.begin("led-wall", false);
  storage.end();
#endif

  pinMode(PIN_LATCH, OUTPUT);
  pinMode(PIN_CLOCK, OUTPUT);
  pinMode(PIN_DATA, OUTPUT);
  pinMode(PIN_ENABLE, OUTPUT);

// server
#ifdef ENABLE_SERVER
  connectToEthernet();

  // set time server
  configTzTime(TZ_INFO, NTP_SERVER);

  initOTA(server);
  initWebsocketServer(server);
  initWebServer();
#endif

  pluginManager.addPlugin(new DrawPlugin());
  pluginManager.addPlugin(new BreakoutPlugin());
  pluginManager.addPlugin(new SnakePlugin());
  pluginManager.addPlugin(new GameOfLifePlugin());
  pluginManager.addPlugin(new StarsPlugin());
  pluginManager.addPlugin(new LinesPlugin());
  pluginManager.addPlugin(new CirclePlugin());
  pluginManager.addPlugin(new RainPlugin());
  pluginManager.addPlugin(new FireworkPlugin());
  pluginManager.addPlugin(new BlobPlugin());

#ifdef ENABLE_SERVER
  pluginManager.addPlugin(new BigClockPlugin());
  pluginManager.addPlugin(new ClockPlugin());
  pluginManager.addPlugin(new PongClockPlugin());
  pluginManager.addPlugin(new TickingClockPlugin());
  pluginManager.addPlugin(new WeatherPlugin());
  pluginManager.addPlugin(new AnimationPlugin());
  pluginManager.addPlugin(new DDPPlugin());
  pluginManager.addPlugin(new ArtNetPlugin());
#endif

  Screen.clear();
  pluginManager.init();
  Scheduler.init();

  btn.onPress(pressHandler).onDoublePress(pressHandler).onPressFor(pressHandler, 1000);
}

TaskHandle_t screenDrawingTaskHandle = NULL;

void screenDrawingTask(void *parameter)
{
  Screen.setup();
  for (;;)
  {
    pluginManager.runActivePlugin();
    vTaskDelay(1);
  }
}

void setup()
{
  baseSetup();
  xTaskCreatePinnedToCore(screenDrawingTask,
                          "screenDrawingTask",
                          10000,
                          NULL,
                          1,
                          &screenDrawingTaskHandle,
                          0);
}

void loop()
{
  static uint8_t taskCounter = 0;

  btn.read();

#ifdef ENABLE_SERVER
  ElegantOTA.loop();
#endif

  if (currentStatus == NONE)
  {
    Scheduler.update();

    if ((taskCounter & 0x03) == 0)
    {
      Messages.scrollMessageEveryMinute();
    }
  }

  // Check network less frequently with exponential backoff
  if (!ETH.hasIP())
  {
    if (!staticFallbackApplied && dhcpStartMillis > 0 &&
        (millis() - dhcpStartMillis) >= dhcpTimeoutMs)
    {
      Serial.println("DHCP timeout, applying fallback static IP");
      ETH.config(IPAddress(192, 168, 1, 91),
                 IPAddress(192, 168, 1, 1),
                 IPAddress(255, 255, 255, 0),
                 IPAddress(1, 1, 1, 1));
      staticFallbackApplied = true;
    }

    unsigned long currentMillis = millis();
    if (currentMillis - lastConnectionAttempt >= reconnectionBackoff)
    {
      Serial.println("Network disconnected, attempting reconnection...");
      logEthStatus();
      connectToEthernet();

      // Exponential backoff: double the wait time, up to max
      reconnectionAttempts++;
      reconnectionBackoff = min(reconnectionBackoff * 2, maxReconnectionBackoff);
    }
  }
  else
  {
    if (reconnectionAttempts > 0)
    {
      Serial.println("Network reconnected successfully");
      reconnectionAttempts = 0;
      reconnectionBackoff = 5000;
    }
  }

  taskCounter++;
  if (taskCounter > 16)
  {
    taskCounter = 0;
  }

#ifdef ENABLE_SERVER
  cleanUpClients();
#endif
  vTaskDelay(1);
}
