# Camada de Voz e IA — Sinric Pro + Google Home + Gemini

Este guia configura a **ponte entre a ESP e o Google Home** (via Sinric Pro) e
explica como a **camada de IA (Gemini)** se encaixa no fluxo de voz.

```
Voz do usuario
      │  "Ok Google, ..."
      ▼
Google Home / Assistente  ──► Gemini (interpretacao de linguagem natural)
      │  comando estruturado (on/off, query)
      ▼  (nuvem Google ↔ nuvem Sinric)
Sinric Pro  ──► WebSocket seguro ──► ESP (firmware)
      │
      ├─ Switch  → liga/desliga o LED atuador
      └─ Sensor  → responde a temperatura do MPU6050
```

---

## Parte 1 — Conta e devices no Sinric Pro

1. Crie uma conta em **https://sinric.pro** (plano gratuito permite 3 devices).
2. No menu **Credentials**, copie:
   - **App Key**
   - **App Secret**

   > Cole esses dois valores em [`ESP32/config.h`](../ESP32/config.h) nos campos
   > `SINRIC_APP_KEY` e `SINRIC_APP_SECRET`.

3. Vá em **Devices → Add Device** e crie **dois** devices:

   | Device | Type | Para que serve | Comando de voz |
   |---|---|---|---|
   | `Irrigacao` (ou `Luz do Jogo`) | **Switch** | Atuador (LED da ESP) | "Ok Google, ligar a luz do jogo" |
   | `Sensor do Jogo` | **Temperature Sensor** | Lê o MPU6050 | "Ok Google, qual a temperatura do sensor do jogo?" |

4. Ao salvar cada device, o Sinric Pro mostra um **Device ID**. Copie:
   - O ID do Switch → `SINRIC_SWITCH_ID` no `config.h`
   - O ID do Temperature Sensor → `SINRIC_TEMP_SENSOR_ID` no `config.h`

---

## Parte 2 — Bibliotecas Arduino

Na Arduino IDE (**Sketch → Include Library → Manage Libraries**) instale:

| Biblioteca | Autor | Uso |
|---|---|---|
| **SinricPro** | Sinric Pro | Ponte com o Google Home |
| **ArduinoJson** | Benoit Blanchon | Telemetria/parse |
| **PubSubClient** | Nick O'Leary | MQTT do ThingsBoard |

> O `SinricPro` puxa `WebSockets` (Markus Sattler) como dependência — aceite quando perguntado.

Selecione a placa correta (**ESP32 Dev Module** ou **NodeMCU 1.0 (ESP-12E)** para ESP8266),
compile e faça upload do [`ESP32/ESP32.ino`](../ESP32/ESP32.ino).

No Serial Monitor (115200 baud) você deve ver:

```
[Sinric] Conectado ao Sinric Pro.
[ThingsBoard] Conectado.
```

---

## Parte 3 — Vincular ao Google Home

1. Abra o app **Google Home** no celular.
2. **+ (Adicionar) → Configurar dispositivo → Funciona com o Google**.
3. Procure por **Sinric Pro** na lista de serviços e selecione.
4. Faça login com a **mesma conta** do Sinric Pro e autorize.
5. Os dois devices (`Irrigacao` e `Sensor do Jogo`) aparecem automaticamente.
   Atribua um cômodo se quiser.

### Comandos de voz para a demonstração

| Comando | O que acontece |
|---|---|
| "Ok Google, **ligar a luz do jogo**" | `onPowerState(true)` no firmware → LED acende |
| "Ok Google, **desligar a luz do jogo**" | LED apaga |
| "Ok Google, **qual a temperatura do sensor do jogo?**" | Google lê o último `sendTemperatureEvent` (MPU6050) |

---

## Parte 4 — Camada de IA (Gemini)

O **Gemini** é o modelo que dá ao Google Home a compreensão de **linguagem natural**.
Você **não precisa de chave de API** para essa integração — ela acontece dentro do
ecossistema Google. O papel do Gemini no fluxo:

- **Interpretação flexível dos comandos.** Sem o Gemini, o Assistente exige frases
  quase exatas. Com o Gemini ativo, variações como *"acende a luzinha do jogo aí"*
  ou *"tá quente no sensor?"* são mapeadas para a mesma ação/consulta do device.
- **Respostas conversacionais.** Ao perguntar a temperatura, o Gemini formula a
  resposta em linguagem natural a partir do valor que a ESP reportou.

### Como habilitar o Gemini no Google Home (para a demo)

1. App **Google Home → foto de perfil → Assistente do Google / Gemini**.
2. Ative **Gemini** como assistente (disponível nas versões recentes do app).
3. Os comandos de voz acima passam a ser interpretados pelo Gemini antes de
   chegarem ao Sinric Pro.

> **Argumentação técnica (defesa):** o dispositivo físico expõe *capabilities*
> padronizadas (Switch, TemperatureSensor) via Sinric Pro. O Gemini atua na
> **borda cognitiva**: traduz a fala do usuário nessas capabilities e verbaliza
> o estado de volta. A ESP permanece agnóstica — ela só recebe `setPowerState`
> ou responde `temperature`, independentemente de como o usuário falou.

---

## Checklist da apresentação

- [ ] Serial Monitor mostrando `[Sinric] Conectado`.
- [ ] "Ligar a luz do jogo" acende o LED físico.
- [ ] "Qual a temperatura do sensor?" responde com valor do MPU6050.
- [ ] Variação informal da frase funciona (mostra o Gemini interpretando).
