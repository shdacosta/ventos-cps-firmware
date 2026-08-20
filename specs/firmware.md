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

**Status: implementado e parcialmente verificado** (`src/wifi_gerenciado.{h,cpp}`, Fase 4).

Requisitos:

1. Conectar na rede ✅ **confirmado ao vivo** — conectou à rede real, IP `192.168.15.129`
2. Reportar conexão e IP no Serial Monitor ✅ **confirmado ao vivo**
3. **Reconectar automaticamente** em queda 💡 implementado (backoff exponencial, teto de 30 s), **mas nunca visto reconectar de verdade** — o sinal ficou estável durante o teste (nenhuma queda natural aconteceu) e o teste ativo (desligar o roteador) foi adiado de propósito para quando o sensor entrar, testando os dois juntos
4. Nunca bloquear a medição — a contagem de pulsos não pode parar durante uma tentativa de reconexão ✅ **confirmado ao vivo** — status ininterrupto a cada 5 s por 35 s seguidos, heap estável (sem vazamento), zero `delay()` no `loop()`

Boas práticas aplicadas:

- Reconexão com **backoff exponencial** e teto — `WiFi.reconnect()`, sem loop apertado
- `WiFi.setSleep(false)` — estação ligada na tomada, latência importa mais que consumo
- Credenciais fora do código versionado (`include/secrets.h`, no `.gitignore`; `secrets.h.example` versionado como template)
- **NTP para timestamp confiável, resolvido e confirmado ao vivo**: o carimbo é do **ESP32**, não do servidor — decisão já tomada no contrato do backend (`measured_at`, ver `backend/README.md` no repo do servidor), por causa do buffer offline da Fase 5. `configTime()` dispara a sincronização assim que conecta; `getLocalTime()` com timeout curto (100 ms) evita bloquear o `loop()` enquanto ainda não sincronizou
- Watchdog: reset automático se o loop principal travar — ✅ **implementado** (`src/watchdog.cpp`: `iniciarWatchdog()` no `setup()`, `esp_task_wdt_reset()` a cada volta do `loop()` e entre lotes de envio, timeout de 30 s — ver [telemetria.md §5](telemetria.md)), **mas o reset em si nunca foi visto acontecendo de verdade** — falta forçar o `loop()` a travar em bancada e confirmar o reinício (mesma verificação ao vivo pendente do restante da Fase 5, ver `telemetria.md §8`)

### ❓ Achado do teste ao vivo: sinal fraco

RSSI medido entre **-82 e -84 dBm** durante todo o teste, na mesma casa do roteador. 📄 Como
referência geral de rádio, abaixo de -80 dBm o Wi-Fi já é considerado fraco/instável — este
valor está no limite do que costuma funcionar de forma confiável.

Relevante para a Fase 6: se o sinal já é fraco perto do roteador, a instalação definitiva no
alto da casa da caixa d'água pode precisar de repetidor Wi-Fi ou antena externa. Ainda não é
urgente, mas entra no radar de pendências de instalação.

### Pendência: reconexão nunca observada em queda real

Não depende do conector dupont — poderia ser testado a qualquer momento desligando o
roteador. Adiado por escolha deliberada: testar reconexão junto com o sensor (Fase 2/3)
numa sessão só, em vez de duas verificações separadas.

---

## 4. Telemetria

Transporte: HTTP em lote — ✅ **decidido e implementado** (Fase 5; `src/telemetria.cpp` + integração em `src/envio.cpp`, que já usa `HTTPClient`/`POST` contra `INGEST_URL`). Design completo — buffer, payload, política de retry — em [telemetria.md](telemetria.md); a lógica pura tem prova nativa, o `HTTPClient` real contra o backend rodando ainda depende de verificação ao vivo (ver `telemetria.md §8`).

Requisitos originais (satisfeitos pelo design de `telemetria.md`, mantidos aqui só como contexto histórico):

- **Buffer offline.** Se o Wi-Fi cair, as leituras não podem ser perdidas. Dimensionar quantas amostras cabem na RAM/NVS e o que fazer ao encher.
- Payload JSON com: velocidade instantânea, média do intervalo, rajada do intervalo, timestamp, contador de sequência
- Contador de sequência permite ao backend detectar lacunas

## 5. Manutenibilidade

- **OTA** — atualizar firmware pelo Wi-Fi. Sem isso, cada correção exige subir na caixa d'água com um notebook.
- **Watchdog** ativo
- Métricas de saúde no payload: uptime, heap livre, RSSI, contagem de reconexões — ✅ **implementadas**, as quatro. As três primeiras desde a Fase 5 (ver [telemetria.md §4](telemetria.md)); `wifi_reconnect_count` num ciclo posterior (ver [reconexoes-wifi.md](reconexoes-wifi.md)) — já em produção. ❓ A contagem de reconexões ainda é hipótese quanto à definição operacional exata: falta confirmar ao vivo que ela não incrementa em "piscadas" de sinal sem queda real (ver `pendencias-hardware.md` item 7)

---

## 6. Configuração do PlatformIO

Ver [`platformio.ini`](../platformio.ini). Pontos relevantes:

- `platform` fixado em `espressif32@^6.9.0` — build reproduzível
- `monitor_speed = 115200` **precisa** casar com o `Serial.begin()` do código
- `esp32_exception_decoder` converte backtrace de crash em `arquivo:linha`
