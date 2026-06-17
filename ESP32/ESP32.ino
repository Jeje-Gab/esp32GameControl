// =====================================================================
//  Velocity Command  -  Firmware da camada de borda (ESP32 / ESP8266)
// ---------------------------------------------------------------------
//  Migracao da Avaliacao Formativa 2 para arquitetura IoT + IA.
//
//  Responsabilidades (executadas de forma CONCORRENTE):
//    1) Ler o MPU6050  -> direcao (left/right/center) + temperatura
//    2) Enviar a direcao para o backend Go (jogo)        -> HTTP POST
//    3) Manter a ponte com o Google Home                 -> Sinric Pro
//         - Switch  : liga/desliga o LED atuador por voz
//         - Sensor  : responde "qual a temperatura?" com o MPU6050
//    4) Publicar telemetria no ThingsBoard               -> MQTT
//
//  CONCORRENCIA:
//    - ESP32   : tarefas FreeRTOS reais (xTaskCreatePinnedToCore).
//                Cada protocolo roda na sua propria task, entao um
//                nunca trava o outro (requisito do enunciado).
//    - ESP8266 : o core Arduino nao expoe FreeRTOS; usamos um
//                escalonador COOPERATIVO nao-bloqueante no loop()
//                (padrao da plataforma). As libs SinricPro/PubSubClient
//                sao callback-driven e nao bloqueiam.
//
//  As credenciais ficam em config.h.
// =====================================================================

#include "config.h"

#if defined(ESP32)
  #include <WiFi.h>
  #include <HTTPClient.h>
  #if WIFI_ENTERPRISE
    // API de WPA2-Enterprise (eduroam). O header/funcoes mudaram entre o
    // core arduino-esp32 2.x e 3.x; cobrimos os dois com __has_include.
    #if __has_include(<esp_eap_client.h>)
      #include <esp_eap_client.h>     // core 3.x
      #define EAP_NEW_API 1
    #else
      #include <esp_wpa2.h>           // core 2.x
      #define EAP_NEW_API 0
    #endif
  #endif
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266HTTPClient.h>
#else
  #error "Placa nao suportada. Use ESP32 ou ESP8266."
#endif

#include <Wire.h>
#include <PubSubClient.h>     // ThingsBoard (MQTT)
#include <ArduinoJson.h>      // Monta o payload de telemetria
#include <SinricPro.h>        // Ponte com Google Home
#include <SinricProSwitch.h>
#include <SinricProTemperaturesensor.h>

// =====================================================================
//  Log
// ---------------------------------------------------------------------
//  LOGF/LOGLN: logs "de regime" (Jogo/ThingsBoard/Sinric). Quando
//  LOG_APENAS_SENSOR esta ligado eles viram no-op, deixando o monitor
//  limpo com apenas a leitura [Sensor] durante os testes.
// =====================================================================
#if LOG_APENAS_SENSOR
  #define LOGF(...)   do {} while (0)
  #define LOGLN(...)  do {} while (0)
#else
  #define LOGF(...)   Serial.printf(__VA_ARGS__)
  #define LOGLN(...)  Serial.println(__VA_ARGS__)
#endif

// =====================================================================
//  MPU6050
// =====================================================================
static int MPU_ADDR = 0x68;   // 0x68 (AD0=LOW) ou 0x69 (AD0=HIGH)

// Pinos I2C
#if defined(ESP8266)
  // SDA -> D2 | SCL -> D1
  static const int PINO_SDA = D2;
  static const int PINO_SCL = D1;
#else
  // ESP32: SDA -> GPIO21 | SCL -> GPIO22 (default)
  static const int PINO_SDA = 21;
  static const int PINO_SCL = 22;
#endif

// Sensibilidade da inclinacao (quanto menor, mais sensivel).
static const int limiteInclinacao = 3500;
// Eixo usado para esquerda/direita ("X", "Y" ou "Z").
// Com o sensor EM PE (vertical), o eixo certo NAO e o que esta alinhado
// com a gravidade (esse so diminui ao inclinar pra qualquer lado -> sempre
// "left"). Escolha o eixo HORIZONTAL, que oscila + de um lado e - do outro.
// Aqui: eixo Y -> accY negativo = "left", accY positivo = "right".
static const char eixoControle = 'Y';
// Inverte left/right se necessario (mantenha false p/ neg=left, pos=right).
static const bool inverterDirecao = false;

// Calibracao do centro do eixo.
static int centroEixo = 0;

// =====================================================================
//  Estado compartilhado entre as tarefas (protegido por mutex no ESP32)
// =====================================================================
struct EstadoSensor {
  String direcao;       // "left" / "right" / "center"
  int16_t accX, accY, accZ;
  float temperaturaC;   // temperatura interna do MPU6050
  bool ledLigado;       // estado do atuador (controlado pelo Google Home)
};

static EstadoSensor estado = {"center", 0, 0, 0, 0.0f, false};

#if defined(ESP32)
  static SemaphoreHandle_t estadoMutex;
  #define TRAVAR()   xSemaphoreTake(estadoMutex, portMAX_DELAY)
  #define LIBERAR()  xSemaphoreGive(estadoMutex)
#else
  // No ESP8266 o loop e cooperativo (single-thread): nao ha concorrencia
  // real, entao o lock e um no-op.
  #define TRAVAR()   do {} while (0)
  #define LIBERAR()  do {} while (0)
#endif

// =====================================================================
//  Clientes de rede
// =====================================================================
// Cada protocolo usa seu proprio WiFiClient para nao haver disputa de
// socket entre as tarefas.
static WiFiClient tbWifiClient;
static PubSubClient tbClient(tbWifiClient);

// =====================================================================
//  Declaracoes
// =====================================================================
void conectarWiFi();
bool iniciarMPU6050();
void calibrarMPU6050();
bool lerMPU6050(int16_t &ax, int16_t &ay, int16_t &az, float &tempC);
String calcularDirecao(int16_t ax, int16_t ay, int16_t az);
int valorEixo(int16_t ax, int16_t ay, int16_t az);
void aplicarLed(bool ligado);

void enviarDirecaoJogo(const String &direcao);
void setupSinric();
void publicarTelemetria();
bool conectarThingsBoard();

// =====================================================================
//  Sinric Pro - callbacks do Google Home
// =====================================================================
// Acionado quando o usuario diz "Ok Google, ligar/desligar <device>".
bool onPowerState(const String &deviceId, bool &state) {
  LOGF("[Sinric] Google Home -> LED %s\n", state ? "LIGADO" : "DESLIGADO");

  TRAVAR();
  estado.ledLigado = state;
  LIBERAR();

  aplicarLed(state);
  return true;   // confirma o comando para o Google Home
}

void setupSinric() {
  SinricProSwitch &meuSwitch = SinricPro[SINRIC_SWITCH_ID];
  meuSwitch.onPowerState(onPowerState);

  SinricPro.onConnected([]() { LOGLN("[Sinric] Conectado ao Sinric Pro."); });
  SinricPro.onDisconnected([]() { LOGLN("[Sinric] Desconectado do Sinric Pro."); });

  SinricPro.begin(SINRIC_APP_KEY, SINRIC_APP_SECRET);
}

// Reporta a temperatura ao Sinric Pro (Google Home le o ultimo valor).
void reportarTemperaturaSinric() {
  TRAVAR();
  float t = estado.temperaturaC;
  LIBERAR();

  SinricProTemperaturesensor &sensor = SinricPro[SINRIC_TEMP_SENSOR_ID];
  sensor.sendTemperatureEvent(t, -1);   // -1 = sem umidade
}

// =====================================================================
//  ThingsBoard - telemetria via MQTT
// =====================================================================
bool conectarThingsBoard() {
  if (tbClient.connected()) return true;
  if (WiFi.status() != WL_CONNECTED) return false;

  tbClient.setServer(TB_HOST, TB_PORT);

  String clientId = "velocity-esp-";
  clientId += String(WiFi.macAddress());

  // No ThingsBoard o Access Token do device entra como USERNAME do MQTT.
  if (tbClient.connect(clientId.c_str(), TB_ACCESS_TOKEN, NULL)) {
    LOGLN("[ThingsBoard] Conectado.");
    return true;
  }

  LOGF("[ThingsBoard] Falha na conexao MQTT, rc=%d\n", tbClient.state());
  return false;
}

void publicarTelemetria() {
  if (!conectarThingsBoard()) return;

  TRAVAR();
  EstadoSensor s = estado;   // copia rapida sob lock
  LIBERAR();

  StaticJsonDocument<256> doc;
  doc["temperature"] = s.temperaturaC;
  doc["direction"]   = s.direcao;
  doc["accX"]        = s.accX;
  doc["accY"]        = s.accY;
  doc["accZ"]        = s.accZ;
  doc["led"]         = s.ledLigado;
  doc["rssi"]        = WiFi.RSSI();

  char payload[256];
  serializeJson(doc, payload, sizeof(payload));

  // Topico padrao de telemetria de device do ThingsBoard.
  tbClient.publish("v1/devices/me/telemetry", payload);

  LOGF("[ThingsBoard] Telemetria -> %s\n", payload);
}

// =====================================================================
//  Jogo - envio da direcao ao backend Go (HTTP POST)
// =====================================================================
void enviarDirecaoJogo(const String &direcao) {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClient client;
  HTTPClient http;

  String url = String("http://") + BACKEND_HOST + ":" + BACKEND_PORT + BACKEND_PATH;

  http.begin(client, url);
  http.setTimeout(1000);
  http.addHeader("Content-Type", "application/json");

  String payload = String("{\"direction\":\"") + direcao + "\"}";
  int codigo = http.POST(payload);
  http.end();

  // Loga so o que importa: movimento (left/right) ou falha de conexao.
  // Fica quieto no caso comum (center + HTTP 200) pra nao poluir o monitor.
  if (direcao != "center" || codigo != 200) {
    LOGF("[Jogo] POST dir=%s -> HTTP %d\n", direcao.c_str(), codigo);
  }
}

// =====================================================================
//  MPU6050
// =====================================================================
// Varre o barramento I2C e lista os enderecos que respondem.
void escanearI2C() {
  Serial.println("[I2C] Escaneando barramento...");
  int encontrados = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[I2C] Dispositivo encontrado em 0x%02X\n", addr);
      encontrados++;
    }
  }
  if (encontrados == 0)
    Serial.println("[I2C] NENHUM dispositivo no barramento. Verifique VCC=3V3, GND, SDA=21, SCL=22 e os jumpers.");
}

bool iniciarMPU6050() {
  // O MPU6050 responde em 0x68 (AD0 em LOW) ou 0x69 (AD0 em HIGH).
  for (int addr : {0x68, 0x69}) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      MPU_ADDR = addr;
      Serial.printf("[MPU] Encontrado em 0x%02X\n", addr);

      // Acorda o MPU6050 (PWR_MGMT_1 = 0x6B).
      Wire.beginTransmission(MPU_ADDR);
      Wire.write(0x6B);
      Wire.write(0);
      return Wire.endTransmission() == 0;
    }
  }
  return false;
}

bool lerMPU6050(int16_t &ax, int16_t &ay, int16_t &az, float &tempC) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);                       // registrador inicial do acelerometro
  if (Wire.endTransmission(false) != 0) return false;

  Wire.requestFrom((uint16_t)MPU_ADDR, (size_t)14, true);
  if (Wire.available() < 14) return false;

  ax = Wire.read() << 8 | Wire.read();
  ay = Wire.read() << 8 | Wire.read();
  az = Wire.read() << 8 | Wire.read();

  int16_t tempRaw = Wire.read() << 8 | Wire.read();   // antes era ignorado!
  tempC = (tempRaw / 340.0f) + 36.53f;                // formula do datasheet

  // gyro X/Y/Z - lidos para esvaziar o buffer, nao usados.
  Wire.read(); Wire.read();
  Wire.read(); Wire.read();
  Wire.read(); Wire.read();

  return true;
}

void calibrarMPU6050() {
  Serial.println("[MPU] Calibrando... mantenha o sensor parado.");
  delay(1500);

  long soma = 0;
  const int amostras = 50;
  int16_t ax, ay, az;
  float t;

  for (int i = 0; i < amostras; i++) {
    lerMPU6050(ax, ay, az, t);
    soma += valorEixo(ax, ay, az);
    delay(30);
  }

  centroEixo = soma / amostras;
  Serial.printf("[MPU] Centro calibrado: %d\n", centroEixo);
}

int valorEixo(int16_t ax, int16_t ay, int16_t az) {
  switch (eixoControle) {
    case 'Y': return ay;
    case 'Z': return az;
    default:  return ax;   // 'X'
  }
}

String calcularDirecao(int16_t ax, int16_t ay, int16_t az) {
  int diferenca = valorEixo(ax, ay, az) - centroEixo;

  String direcao = "center";
  if (diferenca > limiteInclinacao)       direcao = "right";
  else if (diferenca < -limiteInclinacao) direcao = "left";

  if (inverterDirecao) {
    if (direcao == "right") return "left";
    if (direcao == "left")  return "right";
  }
  return direcao;
}

// =====================================================================
//  LED atuador
// =====================================================================
void aplicarLed(bool ligado) {
  bool nivel = LED_ATIVO_BAIXO ? !ligado : ligado;
  digitalWrite(LED_PIN, nivel ? HIGH : LOW);
}

// =====================================================================
//  Wi-Fi
// =====================================================================
void conectarWiFi() {
  Serial.printf("[WiFi] Conectando em %s ...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);

#if defined(ESP32) && WIFI_ENTERPRISE
  // ---- WPA2-Enterprise (eduroam): login + senha, sem senha de rede ----
  #if EAP_NEW_API
    esp_eap_client_set_identity((uint8_t *)EAP_IDENTITY, strlen(EAP_IDENTITY));
    esp_eap_client_set_username((uint8_t *)EAP_USERNAME, strlen(EAP_USERNAME));
    esp_eap_client_set_password((uint8_t *)EAP_PASSWORD, strlen(EAP_PASSWORD));
    esp_wifi_sta_enterprise_enable();
  #else
    esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)EAP_IDENTITY, strlen(EAP_IDENTITY));
    esp_wifi_sta_wpa2_ent_set_username((uint8_t *)EAP_USERNAME, strlen(EAP_USERNAME));
    esp_wifi_sta_wpa2_ent_set_password((uint8_t *)EAP_PASSWORD, strlen(EAP_PASSWORD));
    esp_wifi_sta_wpa2_ent_enable();
  #endif
  WiFi.begin(WIFI_SSID);   // sem senha PSK: a autenticacao e por EAP
#else
  // ---- Rede comum (WPA2-PSK) ----
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
#endif

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (++tentativas >= 40) {
      Serial.println("\n[WiFi] Falha. Reiniciando...");
      delay(2000);
      ESP.restart();
    }
  }

  Serial.printf("\n[WiFi] Conectado. IP: %s\n", WiFi.localIP().toString().c_str());
}

// Diagnostico de rede: sinal, alcance do backend (LAN) e internet.
void diagnosticoRede() {
  Serial.printf("[Rede] SSID: %s | RSSI: %d dBm | IP: %s | Gateway: %s\n",
                WiFi.SSID().c_str(), WiFi.RSSI(),
                WiFi.localIP().toString().c_str(),
                WiFi.gatewayIP().toString().c_str());

  // Teste 1: a ESP alcanca o backend do jogo? (rede local / firewall)
  WiFiClient c1;
  Serial.printf("[Rede] Backend %s:%d ... ", BACKEND_HOST, BACKEND_PORT);
  if (c1.connect(BACKEND_HOST, BACKEND_PORT)) {
    Serial.println("OK (conectou)");
    c1.stop();
  } else {
    Serial.println("FALHOU (backend desligado ou firewall bloqueando a porta)");
  }

  // Teste 2: tem internet de fato? (usado depois por Sinric/ThingsBoard)
  WiFiClient c2;
  Serial.print("[Rede] Internet (google.com:80) ... ");
  if (c2.connect("google.com", 80)) {
    Serial.println("OK (internet ativa)");
    c2.stop();
  } else {
    Serial.println("FALHOU (sem internet ou DNS)");
  }
}

// =====================================================================
//  TAREFAS  -  trabalho concorrente
//  As funcoes abaixo sao o "miolo" de cada tarefa. No ESP32 cada uma
//  roda dentro de uma task FreeRTOS; no ESP8266 sao chamadas pelo
//  escalonador cooperativo do loop().
// =====================================================================

// (1) Le o MPU6050 e atualiza o estado compartilhado.
void trabalhoSensor() {
  int16_t ax, ay, az;
  float tempC;

  if (!lerMPU6050(ax, ay, az, tempC)) return;

  String dir = calcularDirecao(ax, ay, az);

  TRAVAR();
  estado.accX = ax;
  estado.accY = ay;
  estado.accZ = az;
  estado.temperaturaC = tempC;
  estado.direcao = dir;
  LIBERAR();

  // Heartbeat da leitura, com cadencia propria (SENSOR_LOG_INTERVAL_MS) para
  // os testes. Se accX/accY mudam ao inclinar, o MPU6050 e o I2C estao OK.
  static unsigned long ultimoLog = 0;
  unsigned long agora = millis();
  if (agora - ultimoLog >= SENSOR_LOG_INTERVAL_MS) {
    ultimoLog = agora;
    Serial.printf("[Sensor] dir=%s temp=%.1fC accX=%d accY=%d accZ=%d\n",
                  dir.c_str(), tempC, ax, ay, az);
  }
}

// (2) Envia a direcao atual ao backend do jogo.
void trabalhoJogo() {
  TRAVAR();
  String dir = estado.direcao;
  LIBERAR();
  enviarDirecaoJogo(dir);
}

// (4) Publica telemetria + reporta temperatura ao Sinric Pro.
void trabalhoTelemetria() {
  publicarTelemetria();
  reportarTemperaturaSinric();
}

#if defined(ESP32)
// ---------------------------------------------------------------------
//  ESP32  -  tarefas FreeRTOS reais
// ---------------------------------------------------------------------
void taskSensor(void *pv) {
  for (;;) {
    trabalhoSensor();
    vTaskDelay(pdMS_TO_TICKS(30));
  }
}

void taskJogo(void *pv) {
  for (;;) {
    trabalhoJogo();
    vTaskDelay(pdMS_TO_TICKS(100));   // mesma cadencia do projeto original
  }
}

void taskSinric(void *pv) {
  for (;;) {
    SinricPro.handle();               // mantem a ponte com o Google Home viva
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void taskTelemetria(void *pv) {
  for (;;) {
    tbClient.loop();                  // mantem o MQTT vivo
    trabalhoTelemetria();
    vTaskDelay(pdMS_TO_TICKS(TB_TELEMETRY_INTERVAL_MS));
  }
}

void taskWiFi(void *pv) {
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) conectarWiFi();
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}
#endif

// =====================================================================
//  setup()
// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== Velocity Command - ESP (IoT + IA) ===");

  pinMode(LED_PIN, OUTPUT);
  aplicarLed(false);

  conectarWiFi();
  diagnosticoRede();

  Wire.begin(PINO_SDA, PINO_SCL);
  escanearI2C();   // diagnostico: mostra o que esta no barramento I2C
  if (!iniciarMPU6050()) {
    Serial.println("[MPU] AVISO: nao encontrado. Seguindo SEM sensor (direcao fica 'center').");
    Serial.println("[MPU] Resolva a fiacao SDA/SCL e reinicie (botao EN) para reativar.");
  } else {
    calibrarMPU6050();
  }

  setupSinric();
  tbClient.setServer(TB_HOST, TB_PORT);

#if defined(ESP32)
  estadoMutex = xSemaphoreCreateMutex();

  // Cada protocolo na sua propria task. Core 0 cuida da rede pesada
  // (Sinric/Telemetria); core 1 cuida do tempo-real do jogo/sensor.
  xTaskCreatePinnedToCore(taskSensor,     "sensor",     4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(taskJogo,       "jogo",       4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(taskSinric,     "sinric",     8192, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(taskTelemetria, "telemetria", 8192, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(taskWiFi,       "wifi",       4096, NULL, 1, NULL, 0);

  Serial.println("[ESP32] Tarefas FreeRTOS criadas.");
#else
  Serial.println("[ESP8266] Escalonador cooperativo ativo.");
#endif

  Serial.println("Sistema iniciado.\n");
}

// =====================================================================
//  loop()
// =====================================================================
#if defined(ESP32)
void loop() {
  // No ESP32 todo o trabalho esta nas tasks FreeRTOS.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
#else
// ---------------------------------------------------------------------
//  ESP8266  -  escalonador cooperativo nao-bloqueante
// ---------------------------------------------------------------------
void loop() {
  static unsigned long tSensor = 0, tJogo = 0, tTelemetria = 0, tWiFi = 0;
  unsigned long agora = millis();

  // SinricPro e MQTT precisam ser "bombeados" o tempo todo.
  SinricPro.handle();
  tbClient.loop();

  if (agora - tSensor >= 30) {
    trabalhoSensor();
    tSensor = agora;
  }

  if (agora - tJogo >= 100) {
    trabalhoJogo();
    tJogo = agora;
  }

  if (agora - tTelemetria >= TB_TELEMETRY_INTERVAL_MS) {
    trabalhoTelemetria();
    tTelemetria = agora;
  }

  if (agora - tWiFi >= 5000) {
    if (WiFi.status() != WL_CONNECTED) conectarWiFi();
    tWiFi = agora;
  }
}
#endif
