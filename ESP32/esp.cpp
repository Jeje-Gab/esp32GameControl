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

// Controle de envio
unsigned long ultimoEnvio = 0;
const unsigned long intervaloEnvio = 100;

// Log de vida da placa
unsigned long ultimoLogStatus = 0;
const unsigned long intervaloLogStatus = 3000;

// Faixas do joystick
const int limiteEsquerda = 300;
const int limiteDireita = 700;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("=================================");
  Serial.println("ESP8266 Joystick Game");
  Serial.println("Wi-Fi comum");
  Serial.println("=================================");

  pinMode(pinoSW, INPUT_PULLUP);

  conectarWiFi();

  Serial.println();
  Serial.println("Sistema iniciado com sucesso!");
  Serial.println("Movimente o joystick para testar.");
  Serial.println("---------------------------------");
}

void loop() {
  verificarWiFi();

  valorX = analogRead(pinoVRx);
  valorBotao = digitalRead(pinoSW);

  direcaoAtual = obterDirecao(valorX);

  unsigned long agora = millis();

  bool mudouDirecao = direcaoAtual != ultimaDirecaoEnviada;
  bool passouIntervalo = agora - ultimoEnvio >= intervaloEnvio;

  if (mudouDirecao || passouIntervalo) {
    Serial.println();
    Serial.println("========== Leitura do joystick ==========");

    Serial.print("Valor eixo X: ");
    Serial.println(valorX);

    Serial.print("Direcao detectada: ");
    Serial.println(direcaoAtual);

    Serial.print("Botao: ");
    Serial.println(valorBotao == LOW ? "Pressionado" : "Solto");

    bool enviado = enviarDirecao(direcaoAtual);

    if (enviado) {
      Serial.println("Resultado envio: sucesso");
    } else {
      Serial.println("Resultado envio: falha");
    }

    ultimaDirecaoEnviada = direcaoAtual;
    ultimoEnvio = agora;

    Serial.println("=========================================");
  }

  if (agora - ultimoLogStatus >= intervaloLogStatus) {
    Serial.println();
    Serial.println("========== Status da ESP ==========");

    Serial.print("ESP funcionando: SIM");
    Serial.println();

    Serial.print("Wi-Fi: ");
    Serial.println(WiFi.status() == WL_CONNECTED ? "conectado" : "desconectado");

    Serial.print("SSID: ");
    Serial.println(ssid);

    Serial.print("IP da ESP: ");
    Serial.println(WiFi.localIP());

    Serial.print("RSSI sinal Wi-Fi: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");

    Serial.print("Ultima direcao: ");
    Serial.println(direcaoAtual);

    Serial.println("==================================");

    ultimoLogStatus = agora;
  }

  delay(50);
}

void conectarWiFi() {
  Serial.println();
  Serial.println("========== Conexao Wi-Fi ==========");

  Serial.print("Tentando conectar na rede: ");
  Serial.println(ssid);

  Serial.println("Iniciando modo WIFI_STA...");
  WiFi.mode(WIFI_STA);

  Serial.println("Chamando WiFi.begin...");
  WiFi.begin(ssid, password);

  int tentativas = 0;

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    tentativas++;

    Serial.print("Tentando conectar... tentativa ");
    Serial.print(tentativas);
    Serial.print(" | Status: ");
    Serial.println(obterStatusWiFi(WiFi.status()));

    if (tentativas >= 40) {
      Serial.println();
      Serial.println("ERRO: Falha ao conectar no Wi-Fi.");
      Serial.println("Verifique:");
      Serial.println("- Nome da rede Wi-Fi");
      Serial.println("- Senha");
      Serial.println("- Se o roteador do iPhone esta ligado");
      Serial.println("- Se o ESP8266 esta perto do celular");
      Serial.println();
      Serial.println("Reiniciando ESP em 2 segundos...");
      Serial.println("==================================");

      delay(2000);
      ESP.restart();
    }
  }

  Serial.println();
  Serial.println("Wi-Fi conectado com sucesso!");
  Serial.print("SSID conectado: ");
  Serial.println(WiFi.SSID());

  Serial.print("IP da ESP: ");
  Serial.println(WiFi.localIP());

  Serial.print("Gateway: ");
  Serial.println(WiFi.gatewayIP());

  Serial.print("Mascara: ");
  Serial.println(WiFi.subnetMask());

  Serial.print("DNS: ");
  Serial.println(WiFi.dnsIP());

  Serial.print("Sinal Wi-Fi RSSI: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");

  Serial.print("Backend URL configurada: ");
  Serial.println(backendUrl);

  Serial.println("==================================");
}

void verificarWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println();
    Serial.println("ERRO: Wi-Fi desconectado.");
    Serial.println("Tentando reconectar...");

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
    Serial.println("Erro: Wi-Fi desconectado");
    return false;
  }

  WiFiClient client;
  HTTPClient http;

  Serial.println();
  Serial.println("========== Envio HTTP ==========");

  Serial.print("Preparando envio para backend: ");
  Serial.println(backendUrl);

  http.begin(client, backendUrl);
  http.setTimeout(3000);
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"direction\":\"" + direcao + "\"}";

  Serial.print("Payload enviado: ");
  Serial.println(payload);

  Serial.println("Enviando POST...");

  int httpCode = http.POST(payload);

  Serial.print("Codigo HTTP recebido: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    String resposta = http.getString();

    Serial.print("Resposta do backend: ");
    Serial.println(resposta);

    http.end();

    if (httpCode >= 200 && httpCode < 300) {
      Serial.println("POST enviado com sucesso.");
      Serial.println("================================");
      return true;
    }

    Serial.println("POST chegou no backend, mas retornou erro.");
    Serial.println("================================");
    return false;
  }

  Serial.print("Erro HTTP: ");
  Serial.println(http.errorToString(httpCode));

  http.end();

  Serial.println("Falha ao enviar POST.");
  Serial.println("================================");

  return false;
}