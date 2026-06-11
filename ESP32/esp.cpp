#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Wire.h>

// Wi-Fi
const char* ssid = "iPhone";
const char* password = "////1234";

// Backend Go
const char* backendUrl = "http://172.20.10.7:8080/joystick";

// MPU6050
const int MPU_ADDR = 0x68;

// ESP8266 I2C
// SDA -> D2
// SCL -> D1
const int PINO_SDA = D2;
const int PINO_SCL = D1;

// Botao do joystick, caso ainda esteja conectado
const int pinoSW = D5;

int valorBotao = 0;

String direcaoAtual = "center";
String ultimaDirecaoEnviada = "";

// Envio para o backend
unsigned long ultimoEnvio = 0;
const unsigned long intervaloEnvio = 100;

// Log do MPU6050
unsigned long ultimoLogSensor = 0;
const unsigned long intervaloLogSensor = 1000;

// Log de status da ESP
unsigned long ultimoLogStatus = 0;
const unsigned long intervaloLogStatus = 5000;

// Leituras do MPU6050
int16_t accX = 0;
int16_t accY = 0;
int16_t accZ = 0;

int16_t gyroX = 0;
int16_t gyroY = 0;
int16_t gyroZ = 0;

// Calibracao do eixo usado para esquerda/direita
int centroEixo = 0;

// Sensibilidade da inclinacao
// Quanto menor, mais sensivel.
// Valores bons para testar: 2500, 3500, 5000
const int limiteInclinacao = 3500;

// Escolha do eixo usado para controlar esquerda/direita.
// Na maioria dos casos, use "X".
// Se ficar estranho, troque para "Y".
const String eixoControle = "X";

// Se direita e esquerda ficarem invertidas, troque para true.
const bool inverterDirecao = false;

// ---------- declaracoes ----------
void conectarWiFi();
void verificarWiFi();

bool iniciarMPU6050();
void calibrarMPU6050();
bool lerMPU6050();

String obterDirecaoPorMPU();
String direcaoParaTexto(String direcao);
String obterStatusWiFi(wl_status_t status);
bool enviarDirecao(String direcao);

int obterValorEixoControle();

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("======================================");
  Serial.println("       ESP8266 MPU6050 Game");
  Serial.println("======================================");

  pinMode(pinoSW, INPUT_PULLUP);

  conectarWiFi();

  Wire.begin(PINO_SDA, PINO_SCL);

  if (!iniciarMPU6050()) {
    Serial.println("ERRO: MPU6050 nao encontrado.");
    Serial.println("Verifique os fios:");
    Serial.println("VCC -> 3V3");
    Serial.println("GND -> GND");
    Serial.println("SDA -> D2");
    Serial.println("SCL -> D1");
    Serial.println("Reiniciando...");
    delay(3000);
    ESP.restart();
  }

  calibrarMPU6050();

  Serial.println();
  Serial.println("Sistema iniciado com sucesso!");
  Serial.println("Incline o sensor para esquerda ou direita.");
  Serial.println("--------------------------------------");
}

void loop() {
  verificarWiFi();

  bool leituraOk = lerMPU6050();

  if (leituraOk) {
    direcaoAtual = obterDirecaoPorMPU();
  } else {
    Serial.println("Erro ao ler MPU6050. Enviando CENTER por seguranca.");
    direcaoAtual = "center";
  }

  valorBotao = digitalRead(pinoSW);

  unsigned long agora = millis();

  bool passouIntervaloEnvio = agora - ultimoEnvio >= intervaloEnvio;

  if (passouIntervaloEnvio) {
    enviarDirecao(direcaoAtual);

    ultimaDirecaoEnviada = direcaoAtual;
    ultimoEnvio = agora;
  }

  bool passouIntervaloLogSensor = agora - ultimoLogSensor >= intervaloLogSensor;

  if (passouIntervaloLogSensor) {
    Serial.println();
    Serial.println("========== LEITURA DO MPU6050 ==========");

    Serial.print("AccX: ");
    Serial.print(accX);

    Serial.print(" | AccY: ");
    Serial.print(accY);

    Serial.print(" | AccZ: ");
    Serial.println(accZ);

    Serial.print("Eixo de controle: ");
    Serial.print(eixoControle);

    Serial.print(" | Valor eixo: ");
    Serial.print(obterValorEixoControle());

    Serial.print(" | Centro calibrado: ");
    Serial.print(centroEixo);

    Serial.print(" | Limite inclinacao: ");
    Serial.println(limiteInclinacao);

    Serial.print("Direcao detectada: ");
    Serial.print(direcaoParaTexto(direcaoAtual));

    Serial.print(" | Valor enviado ao backend: ");
    Serial.println(direcaoAtual);

    Serial.print("Botao do joystick: ");
    Serial.println(valorBotao == LOW ? "PRESSIONADO" : "SOLTO");

    Serial.println("=========================================");

    ultimoLogSensor = agora;
  }

  bool passouIntervaloLogStatus = agora - ultimoLogStatus >= intervaloLogStatus;

  if (passouIntervaloLogStatus) {
    Serial.println();
    Serial.println("========== STATUS DA ESP ==========");

    Serial.println("ESP funcionando: SIM");

    Serial.print("Wi-Fi: ");
    Serial.println(WiFi.status() == WL_CONNECTED ? "CONECTADO" : "DESCONECTADO");

    Serial.print("IP da ESP: ");
    Serial.println(WiFi.localIP());

    Serial.print("Forca do sinal Wi-Fi RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");

    Serial.print("Ultima direcao lida: ");
    Serial.print(direcaoParaTexto(direcaoAtual));

    Serial.print(" | Valor backend: ");
    Serial.println(direcaoAtual);

    Serial.println("==================================");

    ultimoLogStatus = agora;
  }

  delay(20);
}

bool iniciarMPU6050() {
  Serial.println();
  Serial.println("========== INICIANDO MPU6050 ==========");

  Wire.beginTransmission(MPU_ADDR);
  byte erro = Wire.endTransmission();

  if (erro != 0) {
    Serial.print("MPU6050 nao respondeu. Erro I2C: ");
    Serial.println(erro);
    return false;
  }

  // Acorda o MPU6050
  // Registrador 0x6B = PWR_MGMT_1
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0);
  erro = Wire.endTransmission();

  if (erro != 0) {
    Serial.print("Erro ao acordar MPU6050: ");
    Serial.println(erro);
    return false;
  }

  Serial.println("MPU6050 iniciado com sucesso!");
  Serial.println("=======================================");

  return true;
}

bool lerMPU6050() {
  Wire.beginTransmission(MPU_ADDR);

  // Registrador inicial do acelerometro: 0x3B
  Wire.write(0x3B);

  byte erro = Wire.endTransmission(false);

  if (erro != 0) {
    return false;
  }

  Wire.requestFrom(MPU_ADDR, 14, true);

  if (Wire.available() < 14) {
    return false;
  }

  accX = Wire.read() << 8 | Wire.read();
  accY = Wire.read() << 8 | Wire.read();
  accZ = Wire.read() << 8 | Wire.read();

  // Temperatura, nao usada
  Wire.read() << 8 | Wire.read();

  gyroX = Wire.read() << 8 | Wire.read();
  gyroY = Wire.read() << 8 | Wire.read();
  gyroZ = Wire.read() << 8 | Wire.read();

  return true;
}

void calibrarMPU6050() {
  Serial.println();
  Serial.println("========== CALIBRACAO DO MPU6050 ==========");
  Serial.println("Mantenha o sensor parado na posicao neutra...");
  delay(1500);

  long soma = 0;
  const int amostras = 50;

  for (int i = 0; i < amostras; i++) {
    lerMPU6050();
    soma += obterValorEixoControle();
    delay(30);
  }

  centroEixo = soma / amostras;

  Serial.print("Eixo usado para controle: ");
  Serial.println(eixoControle);

  Serial.print("Centro detectado: ");
  Serial.println(centroEixo);

  Serial.print("Limite de inclinacao: ");
  Serial.println(limiteInclinacao);

  Serial.println("Calibracao concluida!");
  Serial.println("===========================================");
}

int obterValorEixoControle() {
  if (eixoControle == "Y") {
    return accY;
  }

  return accX;
}

String obterDirecaoPorMPU() {
  int valorEixo = obterValorEixoControle();

  int diferenca = valorEixo - centroEixo;

  String direcao = "center";

  if (diferenca > limiteInclinacao) {
    direcao = "right";
  } else if (diferenca < -limiteInclinacao) {
    direcao = "left";
  } else {
    direcao = "center";
  }

  if (inverterDirecao) {
    if (direcao == "right") {
      return "left";
    }

    if (direcao == "left") {
      return "right";
    }
  }

  return direcao;
}

void conectarWiFi() {
  Serial.println();
  Serial.println("========== CONEXAO WI-FI ==========");
  Serial.print("Conectando na rede: ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int tentativas = 0;

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    tentativas++;

    Serial.print("Tentativa ");
    Serial.print(tentativas);
    Serial.print(" | Status: ");
    Serial.println(obterStatusWiFi(WiFi.status()));

    if (tentativas >= 40) {
      Serial.println("ERRO: Nao foi possivel conectar no Wi-Fi.");
      Serial.println("Reiniciando a ESP...");
      delay(2000);
      ESP.restart();
    }
  }

  Serial.println("Wi-Fi conectado com sucesso!");

  Serial.print("IP da ESP: ");
  Serial.println(WiFi.localIP());

  Serial.print("Forca do sinal RSSI: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");

  Serial.println("==================================");
}

void verificarWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println();
    Serial.println("Wi-Fi desconectado. Tentando reconectar...");
    WiFi.disconnect();
    delay(500);
    conectarWiFi();
  }
}

String direcaoParaTexto(String direcao) {
  if (direcao == "left") {
    return "ESQUERDA";
  }

  if (direcao == "right") {
    return "DIREITA";
  }

  return "CENTRO";
}

String obterStatusWiFi(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS:
      return "WL_IDLE_STATUS";

    case WL_NO_SSID_AVAIL:
      return "WL_NO_SSID_AVAIL";

    case WL_SCAN_COMPLETED:
      return "WL_SCAN_COMPLETED";

    case WL_CONNECTED:
      return "WL_CONNECTED";

    case WL_CONNECT_FAILED:
      return "WL_CONNECT_FAILED";

    case WL_CONNECTION_LOST:
      return "WL_CONNECTION_LOST";

    case WL_DISCONNECTED:
      return "WL_DISCONNECTED";

    default:
      return "STATUS_DESCONHECIDO";
  }
}

bool enviarDirecao(String direcao) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Erro: Wi-Fi desconectado. Nao foi possivel enviar.");
    return false;
  }

  WiFiClient client;
  HTTPClient http;

  http.begin(client, backendUrl);
  http.setTimeout(1000);
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"direction\":\"" + direcao + "\"}";

  int httpCode = http.POST(payload);

  if (httpCode > 0) {
    http.end();
    return httpCode >= 200 && httpCode < 300;
  }

  http.end();
  return false;
}