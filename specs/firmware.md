# Firmware (ESP32)

Projeto PlatformIO na raiz do repositório. Framework Arduino sobre ESP-IDF.

Pré-requisitos de hardware e calibração: ver [hardware.md](hardware.md).

---

## 1. Restrições que vêm do sensor

Duas restrições saem direto da tabela de operação. Elas determinam a arquitetura de medição.

### 1.1 Janela fixa de contagem não serve

O sensor opera de **0,15 Hz a 28,4 Hz** — faixa dinâmica de ~200:1.

O código de exemplo do fabricante conta pulsos numa janela fixa de 5 s. Consequências:

| Vento | Pulsos na janela de 5 s | Resultado |
|---|---|---|
| 0,7 km/h | 0 ou 1 (período de 6,8 s > janela) | Inutilizável |
| 5 km/h | 5 | Degraus de ~1 km/h |
| 135 km/h | 142 | Bom |

O produto tem sensibilidade de 0,7 km/h; o algoritmo do fabricante joga essa sensibilidade fora justamente na faixa mais comum do dia a dia. Além disso, uma janela de 5 s **dilui rajadas por definição**.

### 1.2 O debounce tem teto

Período mínimo: **35 ms** (a 135 km/h). Repique de reed switch: tipicamente < 1 ms.

| Debounce | Teto de leitura |
|---|---|
| **3–5 ms** | ~950 km/h — sem risco ✅ |
| 20 ms | 237 km/h — ok, sem folga |
| 50 ms | **95 km/h — corta silenciosamente** ❌ |
| 100 ms | 47 km/h — inutilizável ❌ |

**Decisão: debounce de 5 ms.**

Valores de 50–100 ms são o que a maioria dos tutoriais de reed switch sugere. Aqui eles truncariam as rajadas — e de forma silenciosa, sem nenhum sinal de erro.

⚠️ Se a medição da Fase 2 indicar 2 pulsos por volta, o período mínimo cai para 17,6 ms e essa margem reduz pela metade.

---

## 2. Arquitetura de medição

Cada grandeza pede um método diferente. Usar os dois simultaneamente:

| Grandeza | Método | Por quê |
|---|---|---|
| **Velocidade atual** | Período entre os dois últimos pulsos (`micros()` na ISR) | Um único pulso já dá leitura precisa — resolve o vento fraco |
| **Velocidade média** | Contagem de pulsos no intervalo | Para média, contagem é matematicamente correto, não aproximação |
| **Rajada** | Máximo da média móvel de **3 s** (padrão WMO) | Contagem não tem resolução temporal suficiente |
| **Calmaria** | Sem pulso por **10 s** → 0 km/h | Partida (0,7 km/h) tem período de 6,8 s; timeout menor geraria falso zero |

### Regras da ISR

- Marcada com `IRAM_ATTR` — precisa residir na RAM interna
- Faz o mínimo: carimba `micros()`, valida debounce, incrementa contador, sai
- **Zero ponto flutuante, zero `Serial`, zero alocação** dentro da ISR
- Todas as variáveis compartilhadas com o loop declaradas `volatile`
- Leitura de valores multi-byte no loop protegida contra corrida (seção crítica ou cópia atômica)

### Erros do código de exemplo a não repetir

O programa do fabricante é para Arduino UNO e tem defeitos que não devem ser portados:

| Problema | Correção |
|---|---|
| `attachInterrupt(0, ...)` → GPIO 0 no ESP32 | `digitalPinToInterrupt(PINO_SENSOR)` |
| `counter` sem `volatile` | `volatile` + `IRAM_ATTR` na ISR |
| Sem debounce nenhum | 5 ms |
| `while(millis() < inicio + periodo)` — estoura em 49 dias | `millis() - inicio < periodo` |
| Aritmética inteira destrói resolução do RPM | Ponto flutuante, ou período direto |
| Busy-wait bloqueante de 5 s | Loop não bloqueante |
| `attachInterrupt` sem `detachInterrupt`, a cada iteração | Anexar uma vez no `setup()` |

---

## 3. Wi-Fi

Requisitos:

1. Conectar na rede
2. Reportar conexão e IP no Serial Monitor
3. **Reconectar automaticamente** em queda
4. Nunca bloquear a medição — a contagem de pulsos não pode parar durante uma tentativa de reconexão

Boas práticas a aplicar:

- Reconexão com **backoff exponencial** e teto — não tentar em loop apertado
- `WiFi.setSleep(false)` se a latência importar; manter sleep se o consumo importar
- Credenciais fora do código versionado (`include/secrets.h`, já no `.gitignore`)
- **NTP** para timestamp confiável — decidir se o carimbo de hora é do ESP32 ou do servidor
- Watchdog: reset automático se o loop principal travar

---

## 4. Telemetria

Transporte: MQTT ou HTTP — **a decidir na Fase 5**.

Requisitos:

- **Buffer offline.** Se o Wi-Fi cair, as leituras não podem ser perdidas. Dimensionar quantas amostras cabem na RAM/NVS e o que fazer ao encher.
- Payload JSON com: velocidade instantânea, média do intervalo, rajada do intervalo, timestamp, contador de sequência
- Contador de sequência permite ao backend detectar lacunas

## 5. Manutenibilidade

- **OTA** — atualizar firmware pelo Wi-Fi. Sem isso, cada correção exige subir na caixa d'água com um notebook.
- **Watchdog** ativo
- Métricas de saúde no payload: uptime, heap livre, RSSI, contagem de reconexões

---

## 6. Configuração do PlatformIO

Ver [`platformio.ini`](../platformio.ini). Pontos relevantes:

- `platform` fixado em `espressif32@^6.9.0` — build reproduzível
- `monitor_speed = 115200` **precisa** casar com o `Serial.begin()` do código
- `esp32_exception_decoder` converte backtrace de crash em `arquivo:linha`
