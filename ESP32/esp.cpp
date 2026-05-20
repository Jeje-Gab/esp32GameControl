#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

// Wi-Fi
const char* ssid = "iPhone";
const char* password = "////1234";

// Backend Go
const char* backendUrl = "http://172.20.10.7:8080/joystick";

// Joystick
const int pinoVRx = A0;
const int pinoSW = D5;

int valorX = 0;
int valorBotao = 0;

String direcaoAtual = "center";
String ultimaDirecaoEnviada = "";

// Envio para o backend
// Deixe baixo para o carro ficar mais liso
unsigned long ultimoEnvio = 0;
const unsigned long intervaloEnvio = 100;

// Log do joystick
// Deixe mais alto para conseguir ler o Serial Monitor
unsigned long ultimoLogJoystick = 0;
const unsigned long intervaloLogJoystick = 1000;

// Log de status da ESP
unsigned long ultimoLogStatus = 0;
const unsigned long intervaloLogStatus = 5000;

// Faixas do joystick — ajustadas pela calibracao no setup()
int centroVRx = 512;
int limiteEsquerda = 300;
int limiteDireita = 700;

// ---------- declaracoes ----------
void conectarWiFi();
void verificarWiFi();
void calibrarJoystick();
String obterDirecao(int valorX);
String direcaoParaTexto(String direcao);
String obterStatusWiFi(wl_status_t status);
bool enviarDirecao(String direcao);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("======================================");
  Serial.println("       ESP8266 Joystick Game");
  Serial.println("======================================");

  pinMode(pinoSW, INPUT_PULLUP);

  conectarWiFi();
  calibrarJoystick();

  Serial.println();
  Serial.println("Sistema iniciado com sucesso!");
  Serial.println("Movimente o joystick para testar.");
  Serial.println("--------------------------------------");
}

void loop() {
  verificarWiFi();

  valorX = analogRead(pinoVRx);
  valorBotao = digitalRead(pinoSW);

  direcaoAtual = obterDirecao(valorX);

  unsigned long agora = millis();

  // Envia para o backend de forma rápida para deixar o movimento liso
  bool passouIntervaloEnvio = agora - ultimoEnvio >= intervaloEnvio;

  if (passouIntervaloEnvio) {
    enviarDirecao(direcaoAtual);

    ultimaDirecaoEnviada = direcaoAtual;
    ultimoEnvio = agora;
  }

  // Log do joystick mais lento para conseguir ler
  bool passouIntervaloLogJoystick = agora - ultimoLogJoystick >= intervaloLogJoystick;

  if (passouIntervaloLogJoystick) {
    Serial.println();
    Serial.println("========== LEITURA DO JOYSTICK ==========");

    Serial.print("Valor eixo X: ");
    Serial.print(valorX);

    Serial.print(" | Centro: ");
    Serial.print(centroVRx);

    Serial.print(" | Limite esquerda: ");
    Serial.print(limiteEsquerda);

    Serial.print(" | Limite direita: ");
    Serial.println(limiteDireita);

    Serial.print("Direcao detectada: ");
    Serial.print(direcaoParaTexto(direcaoAtual));
    Serial.print(" | Valor enviado ao backend: ");
    Serial.println(direcaoAtual);

    Serial.print("Botao do joystick: ");
    Serial.println(valorBotao == LOW ? "PRESSIONADO" : "SOLTO");

    Serial.println("=========================================");

    ultimoLogJoystick = agora;
  }

  // Log de status da ESP mais espaçado
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

  // Delay pequeno para nao travar o movimento
  delay(20);
}

// Le 30 amostras com o joystick em repouso e calcula os limites automaticamente.
// Mantenha o joystick solto ao ligar a ESP.
void calibrarJoystick() {
  Serial.println();
  Serial.println("========== CALIBRACAO DO JOYSTICK ==========");
  Serial.println("Mantenha o joystick SOLTO na posicao neutra...");
  delay(1500);

  long soma = 0;
  const int amostras = 30;

  for (int i = 0; i < amostras; i++) {
    soma += analogRead(pinoVRx);
    delay(30);
  }

  centroVRx = (int)(soma / amostras);

  int rangeEsquerda = centroVRx;
  int rangeDireita = 1023 - centroVRx;

  limiteEsquerda = centroVRx - max(1, rangeEsquerda / 3);
  limiteDireita = centroVRx + max(1, rangeDireita / 3);

  if (limiteEsquerda < 0) {
    limiteEsquerda = 0;
  }

  if (limiteDireita > 1023) {
    limiteDireita = 1023;
  }

  Serial.print("Centro detectado: ");
  Serial.println(centroVRx);

  Serial.print("Limite para ESQUERDA: ");
  Serial.println(limiteEsquerda);

  Serial.print("Limite para DIREITA: ");
  Serial.println(limiteDireita);

  Serial.println("Calibracao concluida!");
  Serial.println("============================================");
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

String obterDirecao(int valorX) {
  if (valorX < limiteEsquerda) {
    return "left";
  }

  if (valorX > limiteDireita) {
    return "right";
  }

  return "center";
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