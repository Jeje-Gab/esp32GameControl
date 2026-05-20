# Technical Debt

## [TD-001] Comunicação ESP → Backend via HTTP POST

### Situação atual

A ESP8266 envia a direção do joystick ao backend usando **HTTP POST** a cada 100ms:

```
ESP8266  →  POST /joystick (JSON)  →  Backend Go
```

Isso funciona, mas o HTTP é um protocolo pesado para esse caso de uso. Cada envio carrega:
- Handshake TCP (ou manutenção de keep-alive)
- Headers HTTP de request e response
- Parse de JSON no response que a ESP ignora

O payload útil é `{"direction":"left"}` — 20 bytes. O overhead é dezenas de vezes maior que o dado em si.

Além disso, o HTTP é **confiável por natureza** (TCP com retransmissão), o que é prejudicial aqui: se um pacote se perder, o TCP tentará reenviar uma informação já desatualizada. Para dados efêmeros como posição de joystick, a retransmissão piora a experiência em vez de melhorá-la.

---

## Opções de melhoria

### Opção A — UDP (recomendado para este projeto)

**O que é:** Protocolo de transporte sem conexão e sem garantia de entrega. A ESP dispara o pacote e não espera resposta.

**Por que faz sentido aqui:**
- O dado é efêmero — se um pacote for perdido, o próximo chega em 100ms com o estado atual. Reenviar o perdido seria entregar informação velha.
- Overhead mínimo: sem handshake, sem headers, sem ACK.
- A ESP8266 tem suporte nativo via `WiFiUDP` — implementação simples.
- Sem dependências externas. Nenhum serviço adicional para rodar.

**Ganhos esperados:**

| Métrica | HTTP POST | UDP |
|---|---|---|
| Overhead por envio | ~500–800 bytes (headers) | ~8 bytes (header UDP) |
| Latência por envio | 5–20ms (TCP + HTTP) | < 1ms |
| Retransmissão em perda | Sim (prejudicial) | Não (correto) |
| Dependência externa | Nenhuma | Nenhuma |

**Como ficaria o código:**

ESP (`esp.cpp`):
```cpp
#include <WiFiUdp.h>

WiFiUDP udp;
const char* backendIP = "172.20.10.7";
const int backendPort = 4210;

void enviarDirecao(String direcao) {
    String payload = "{\"direction\":\"" + direcao + "\"}";
    udp.beginPacket(backendIP, backendPort);
    udp.print(payload);
    udp.endPacket();
}
```

Backend (`main.go`):
```go
func listenUDP() {
    addr, _ := net.ResolveUDPAddr("udp", ":4210")
    conn, _ := net.ListenUDP("udp", addr)
    defer conn.Close()

    buf := make([]byte, 64)
    for {
        n, _, _ := conn.ReadFromUDP(buf)

        var input JoystickInput
        if err := json.Unmarshal(buf[:n], &input); err != nil {
            continue
        }

        // mesmo processamento do handleJoystick atual
        processJoystickInput(input)
    }
}
```

**Desvantagens:**
- Sem confirmação de entrega (aceitável e desejável aqui)
- Pode ter problemas com alguns firewalls ou NATs restritivos
- Não escala bem se o projeto crescer para múltiplos dispositivos ou tópicos

---

### Opção B — MQTT com QoS 0

**O que é:** Protocolo de mensageria leve para IoT, amplamente adotado no mercado. QoS 0 significa fire-and-forget — sem garantia de entrega, equivalente ao UDP em termos de confiabilidade, mas sobre TCP.

**Por que faz sentido:**
- Padrão da indústria para ESP + backend em projetos IoT
- QoS 0 elimina o problema da retransmissão indesejada
- Suporte excelente na ESP8266 via biblioteca `PubSubClient`
- Permite múltiplos subscribers no mesmo tópico (ex: um dashboard de telemetria além do jogo)
- Escalável: adicionar novos dispositivos ou tópicos é trivial

**Ganhos esperados:**

| Métrica | HTTP POST | MQTT QoS 0 |
|---|---|---|
| Overhead por envio | ~500–800 bytes | ~2–5 bytes (header MQTT fixo) |
| Latência por envio | 5–20ms | 1–3ms |
| Retransmissão em perda | Sim (prejudicial) | Não (QoS 0) |
| Escalabilidade | Baixa | Alta |
| Dependência externa | Nenhuma | Broker MQTT (ex: Mosquitto) |

**Como ficaria o código:**

ESP (`esp.cpp`):
```cpp
#include <PubSubClient.h>

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

void conectarMQTT() {
    mqtt.setServer("172.20.10.7", 1883);
    mqtt.connect("esp8266-joystick");
}

void enviarDirecao(String direcao) {
    String payload = "{\"direction\":\"" + direcao + "\"}";
    // QoS 0 = fire and forget, sem retenção
    mqtt.publish("game/joystick", payload.c_str(), false);
}
```

Backend (`main.go`):
```go
// Subscriber MQTT no tópico game/joystick
func onMQTTMessage(client mqtt.Client, msg mqtt.Message) {
    var input JoystickInput
    if err := json.Unmarshal(msg.Payload(), &input); err != nil {
        return
    }
    processJoystickInput(input)
}
```

**Desvantagens:**
- Requer um broker MQTT rodando (ex: `docker run -d -p 1883:1883 eclipse-mosquitto`)
- Adiciona uma dependência de infraestrutura ao projeto
- Levemente mais complexo de configurar que UDP

---

## Comparação final

| Critério | HTTP POST (atual) | UDP | MQTT QoS 0 |
|---|---|---|---|
| Latência | Alta | Mínima | Baixa |
| Overhead | Alto | Mínimo | Baixo |
| Comportamento em perda | Retransmite (ruim) | Ignora (correto) | Ignora (correto) |
| Complexidade de implementação | Baixa | Baixa | Média |
| Dependência externa | Nenhuma | Nenhuma | Broker MQTT |
| Escalabilidade | Baixa | Baixa | Alta |
| Padrão IoT | Não | Não | Sim |

---

## Recomendação

- **Projeto atual (jogo local):** migrar para **UDP**. Ganho imediato em latência e overhead sem nenhuma dependência nova. Implementação simples em ambos os lados.
- **Projeto com crescimento futuro (múltiplos dispositivos, telemetria, cloud):** migrar para **MQTT QoS 0**. O custo do broker é compensado pelo ecossistema e escalabilidade.
