# Camada de Monitoramento — ThingsBoard (Dashboard + Alerta Push)

Este guia provisiona o device no ThingsBoard, cria o **dashboard** dos sensores e
configura a **rule chain** que dispara uma **notificação push** no app mobile do
ThingsBoard quando a leitura entra em estado crítico.

```
ESP ──MQTT (v1/devices/me/telemetry)──► ThingsBoard
                                          ├─ Dashboard (widgets em tempo real)
                                          └─ Rule Chain ──► Push notification (app mobile)
```

A ESP publica este payload a cada 5s (ver `publicarTelemetria()` no firmware):

```json
{ "temperature": 28.4, "direction": "left", "accX": 1200,
  "accY": -340, "accZ": 16100, "led": true, "rssi": -57 }
```

---

## Parte 1 — Provisionar o device

1. Crie conta gratuita em **https://thingsboard.cloud**
   (ou use o sandbox **https://demo.thingsboard.io**).
2. **Devices → +  → Add new device**:
   - Name: `Velocity Command ESP`
   - Device profile: `default`
3. Abra o device → aba **Credentials** → copie o **Access Token**.
   - Cole em [`ESP32/config.h`](../ESP32/config.h) no campo `TB_ACCESS_TOKEN`.
   - Confirme `TB_HOST` (`thingsboard.cloud` ou `demo.thingsboard.io`) e `TB_PORT` (1883).
4. Faça upload do firmware. Em **Devices → Velocity Command ESP → Latest telemetry**
   você deve ver `temperature`, `direction`, `accX/Y/Z`, `led`, `rssi` chegando.

---

## Parte 2 — Dashboard

### Opção A — Importar pronto (rápido)

1. **Dashboards → +  → Import dashboard**.
2. Selecione [`thingsboard/dashboard.json`](../thingsboard/dashboard.json).
3. Ao importar, faça o **alias** `Velocity Command ESP` apontar para o seu device.

> ⚠️ O schema de dashboard muda entre versões do ThingsBoard. Se o import
> reclamar de algum widget, use a **Opção B** (montagem manual) — é rápida e
> garantida. O `dashboard.json` serve como referência da estrutura desejada.

### Opção B — Montar manualmente

1. **Dashboards → +  → Create new dashboard** → `Velocity Command`.
2. **Add widget → Charts → Timeseries Line Chart**: datakey `temperature`.
3. **Add widget → Cards → Value Card**: datakey `direction` (string).
4. **Add widget → Charts → Timeseries Line Chart**: datakeys `accX`, `accY`, `accZ`.
5. **Add widget → Analogue gauges → Temperature**: datakey `temperature`, range 0–60.
6. **Add widget → Cards → Signal strength**: datakey `rssi`.

Para o **link público**: abra o dashboard → ícone de **compartilhar (Make dashboard public)**
→ copie a URL pública (esse é o link a entregar no Moodle).

---

## Parte 3 — Rule Chain: alerta push

Objetivo: quando `temperature > 40`, criar um **alarme** e enviar **push** ao app
mobile do ThingsBoard.

### 3.1 — App mobile

Instale **ThingsBoard** (Play Store / App Store), faça login na mesma conta.
Em **thingsboard.cloud → Notification center → Mobile app settings**, garanta que o
app está pareado (necessário para receber push).

O fluxo tem **duas partes**: (a) a **rule chain** cria um *alarme* quando a
temperatura passa do limite; (b) o **Notification Center** observa esse alarme e
manda o **push**. Essa separação é como o ThingsBoard atual faz push — é mais
robusta do que um nó de notificação dentro da rule chain (que dependeria de
UUIDs de template/destino que só existem na sua conta).

```
[Input] ──► [Filtro: temperature > 40] ──true──► [Create Alarm "Temperatura Alta"]
                                                            │
                                          (Notification Center observa o alarme)
                                                            ▼
                                                  Push no app mobile
```

### 3.2 — Criar o alarme (rule chain)

**Importar pronto:**
1. **Rule chains → +  → Import rule chain** → selecione
   [`thingsboard/rule-chain-alerta.json`](../thingsboard/rule-chain-alerta.json).
2. Abra a **Root Rule Chain**, adicione um nó **Flow → rule chain** apontando para
   `Alerta Temperatura`, e ligue a saída **Post telemetry** (Message Type Switch)
   a ele. **Save**.

**Ou manualmente** na Root Rule Chain:
1. Nó **Filter → script**: `return msg.temperature > 40;`
2. Saída **True** → nó **Action → create alarm**:
   - Alarm type: `Temperatura Alta` · Severity: `CRITICAL`
3. **Save**.

### 3.3 — Configurar o push (Notification Center)

1. **Notification center → Notifications → Recipients** → crie um grupo com seu
   usuário (canal **Mobile app**).
2. **Notification center → Notifications → Templates** → novo template, método
   **Mobile app**, mensagem ex.: `Temperatura crítica: ${temperature}°C`.
3. **Notification center → Notification rules → +**:
   - Trigger: **Alarm**
   - Alarm type: `Temperatura Alta`
   - Template/Recipients: os criados acima.
4. **Save**. A partir daí, todo alarme `Temperatura Alta` gera um push.

### 3.4 — Testar o alerta

Como o MPU6050 mede a temperatura do próprio chip (~30–40 °C em uso), para forçar
o alarme na demo você pode:

- **Aquecer levemente o sensor** com o dedo até passar de 40 °C, **ou**
- Baixar o limite temporariamente para `> 30` no nó de filtro durante a demo.

Quando a condição dispara: aparece um **alarme** em **Alarms** e chega um **push**
no celular. 

---

## Checklist da apresentação

- [ ] Latest telemetry recebendo dados a cada 5s.
- [ ] Dashboard público abrindo com gráficos atualizando ao vivo.
- [ ] Inclinar o sensor muda `direction` no card em tempo real.
- [ ] Aquecer o sensor dispara alarme + **push** no app mobile.
