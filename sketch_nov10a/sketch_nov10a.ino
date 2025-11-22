#include <Wire.h>
#include <WiFi.h>               // Inclui a biblioteca WiFi para conectar ao WiFi
#include "html_page.h"          // Inclui o arquivo HTML que contém a página web
#include <ESPAsyncWebServer.h>  // Inclui a biblioteca para servidor web assíncrono
#include <AsyncTCP.h>           // Inclui a biblioteca para TCP assíncrono
#include <Adafruit_NeoPixel.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#define SEALEVELPRESSURE_HPA (1013.25)
#define BME280_I2C_SDA_PIN 6
#define BME280_I2C_SCL_PIN 7

Adafruit_BME280 bme;

const uint8_t LED_PIN = 8;  // Define o pino do LED
const uint8_t NUM_LED = 1;  // Define o pino do botão

Adafruit_NeoPixel ledRgb(NUM_LED, LED_PIN, NEO_GRB + NEO_KHZ800);

uint8_t brightness;
bool statusLED;

const uint32_t timeNotify = 2000;  // Tempo em milissegundos para notificar os clientes

// Credenciais de rede WiFi
const char *ssid = "DTEL_N_INTERNET";
const char *password = "wrn30000";

// Criação do servidor web e WebSocket
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Evento WiFi IP
void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info) {
  Serial.println("WiFi Conectado, IP:");
  Serial.println(WiFi.localIP());
}

// Evento WiFi Desconectado
void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  Serial.println("Desconectado do WiFi");
  Serial.print("WiFi perdeu a conexão. Razão: ");
  Serial.println(info.wifi_sta_disconnected.reason);
  Serial.println("Tentando reconectar");
  WiFi.begin(ssid, password);
}

// Função para notificar todos os clientes conectados com o estado atual
void notifyClients() {
  float temp = bme.readTemperature();
  float press = bme.readPressure() / 100.0F;
  float umid = bme.readHumidity();
  float alt = bme.readAltitude(SEALEVELPRESSURE_HPA);

  String msg = "TEMP=" + String(temp) + ";" + "PRESS=" + String(press) + ";" + "UMID=" + String(umid) + ";" + "ALT=" + String(alt) + ";" + "LED=" + String(statusLED ? "on" : "off") + ";" + "PWM=" + String(brightness) + ";";
  ws.textAll(msg);
}

// Função para definir o estado do LED e notificar os clientes
void setLED(bool state) {
  statusLED = state;

  if (statusLED == true && brightness != 0) {
    ledRgb.setPixelColor(0, 0, 255, 0);
    ledRgb.show();
  } else {
    ledRgb.clear();
    ledRgb.show();
    statusLED = false;
  }

  String msgL = "LED=" + String(statusLED ? "on" : "off") + ";";
  ws.textAll(msgL);
}


void setPWM(int value) {
  brightness = value;
  ledRgb.setBrightness(brightness);
  ledRgb.show();
  setLED(brightness > 0);

  String msgP = "PWM=" + String(brightness) + ";";
  ws.textAll(msgP);
}

// Função para lidar com eventos do WebSocket
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%lu connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      client->text(String(statusLED ? "on" : "off"));
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      {
        String msg = "";
        for (size_t i = 0; i < len; i++) {
          msg += (char)data[i];
        }
        Serial.println("Mensagem recebida via WebSocket: " + msg);
        if (msg == "on") {
          setLED(true);
        } else if (msg == "off") {
          setLED(false);
        } else if (msg.startsWith("PWM=")){
          setPWM(msg.substring(4).toInt());
        }
        notifyClients();
        break;
      }
    default:
      break;
  }
}

void handleRoot(AsyncWebServerRequest *request) {
  Serial.println("Acessando a página principal");
  request->send(200, "text/html", index_html);  // Retorna o HTML para o cliente
}

void handleNotFound(AsyncWebServerRequest *request) {
  Serial.println("Página não encontrada");
  request->send(404, "text/plain", "Not found");  // Retorna uma resposta 404 Not Found
}

void setup() {
  Serial.begin(115200);
  bool status;
  Wire.begin(6, 7);
  status = bme.begin(0x76, &Wire);
  if (!status) {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    while (1);
  }

  WiFi.mode(WIFI_STA);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);

  WiFi.disconnect(true);  // Desconecta de qualquer rede WiFi
  delay(1000);            // Aguarda 1 segundo

  // Registra os eventos do WiFi
  WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(WiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  //Conecta na rede WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  ws.onEvent(onWebSocketEvent);  // Registra o evento do WebSocket
  server.addHandler(&ws);        // Adiciona o WebSocket ao servidor

  // Definicao de Rotas do Servidor (como um Backend)
  server.on("/", HTTP_GET, handleRoot);
  server.onNotFound(handleNotFound);

  server.begin();  // Inicia o servidor web
  Serial.println("Servidor iniciado");
}

void loop() {
    // Notifica clientes a cada timeNotify milissegundos
  static uint32_t lastUpdate = 0;
  if (millis() - lastUpdate >= timeNotify) {
    lastUpdate = millis();
    notifyClients();
  }
  ws.cleanupClients();
  delay(10);  // Pequena pausa para evitar sobrecarga do loop
}