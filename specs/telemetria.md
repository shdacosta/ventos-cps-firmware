# Spec — Telemetria (Fase 5)

**Design aprovado em 2026-08-19.**

Pré-requisitos já decididos: [medicao.md](medicao.md) (produz `Amostra`, já
implementado), [firmware.md §4-5](firmware.md#4-telemetria) (requisitos
originais de telemetria/manutenibilidade), `backend/README.md` no repo do
servidor (contrato de ingestão — fonte única do payload e das regras de
retry, citado aqui, nunca duplicado sem necessidade).

---

## 1. Objetivo

Pegar cada `Amostra` que a Fase 2+3 já produz a cada 10 s e fazê-la chegar no
backend via `POST /api/v1/ingest`, em lotes de 60 s (6 amostras), sobrevivendo
a quedas de Wi-Fi sem perder o que já foi confirmado — mais três preocupações
de manutenibilidade que andam junto por serem pequenas e definidas nesta
mesma fase: watchdog e OTA.

Fora do que o backend já define — citado aqui, decidido lá: formato do
payload, limite de 500 amostras/lote, regra de retry por faixa de status
HTTP, ausência de rate limit no ingest (`backend/README.md`).

---

## 2. Decisão estruturante: separar lógica pura de I/O real

Mesma fronteira que já funcionou na Fase 2+3:

| Módulo | Toca hardware/rede? | Como se verifica |
|---|---|---|
| `telemetria.{h,cpp}` | Não — buffer circular, decisão de retry, montagem do payload JSON | **Testes nativos** (`env:native`), incluindo o JSON — `ArduinoJson` não depende de `Arduino.h` |
| Integração em `main.cpp` (`HTTPClient`, `ArduinoOTA`, `esp_task_wdt`) | Sim | Só por compilação + revisão + teste ao vivo, mesma situação do `anemometro.cpp` |

O motivo é o mesmo de antes: a parte mais fácil de errar em silêncio (quando
descartar um lote vs. quando manter tentando, como o buffer circular estoura,
se o JSON tem o campo certo) fica com prova automatizada. A parte que só
existe com rede/hardware de verdade fica reduzida ao mínimo.

---

## 3. `telemetria.h` — buffer, retry, payload (lógica pura)

```cpp
#pragma once
#include <cstdint>

// ~4h de buffer a 10s/amostra. RAM, nao NVS -- ver §9 sobre a decisao de
// perder o buffer nao-enviado em caso de reinicio.
constexpr uint32_t CAPACIDADE_BUFFER_TELEMETRIA = 1440;

// Limite do backend por requisicao (backend/README.md).
constexpr uint32_t MAX_AMOSTRAS_POR_LOTE = 500;

// Teto de lotes seguidos numa unica tentativa de esvaziamento. Dado
// CAPACIDADE_BUFFER_TELEMETRIA=1440 e MAX_AMOSTRAS_POR_LOTE=500, 3 lotes
// (ceil(1440/500)) ja bastam pra esvaziar o buffer inteiro numa unica
// passada -- este teto existe pra nao depender de recalcular esse numero
// a mao se a capacidade do buffer mudar no futuro.
constexpr uint32_t MAX_LOTES_POR_CICLO = 3;

struct AmostraTelemetria {
    uint32_t measuredAt;   // unix UTC, segundos -- so grava se o relogio
                            // (NTP) ja estiver sincronizado
    float    avgSpeedMs;
    float    gustSpeedMs;
};

struct BufferTelemetria {
    AmostraTelemetria amostras[CAPACIDADE_BUFFER_TELEMETRIA];
    uint32_t inicio;                  // indice circular da amostra mais antiga
    uint32_t quantidade;              // quantas amostras validas tem agora
    uint32_t descartadasPorOverflow;  // contador so-diagnostico (Serial),
                                        // no payload nao entra -- schema do
                                        // backend ja esta fechado
};

// Insere uma amostra. Se o buffer estiver cheio, sobrescreve a mais antiga
// (poli­tica: preserva o mais recente, ver §9) e incrementa
// descartadasPorOverflow.
void inserirAmostra(BufferTelemetria& buffer, const AmostraTelemetria& amostra);

// Copia ate `capacidadeSaida` das amostras mais antigas para `saida`, SEM
// remover do buffer -- quem chama decide se remove, depois de saber o
// resultado do envio. Devolve quantas copiou (pode ser menos que
// capacidadeSaida se o buffer tiver menos amostras que isso).
uint32_t copiarProximoLote(const BufferTelemetria& buffer,
                            AmostraTelemetria* saida, uint32_t capacidadeSaida);

// Remove as `n` amostras mais antigas -- chamado depois de confirmar que
// foram aceitas (200) ou definitivamente rejeitadas (4xx).
void removerMaisAntigas(BufferTelemetria& buffer, uint32_t n);

enum class AcaoAposResposta {
    Remover,    // 2xx -- lote aceito, remove do buffer
    Descartar,  // 4xx -- lote invalido (payload malformado, token errado,
                // lote grande demais) -- reenviar o mesmo lote so repetiria
                // o mesmo erro, entao descarta e loga
    Manter,     // rede/timeout, ou 5xx -- problema transitorio, mantem no
                // buffer para a proxima tentativa
};

// codigo seguindo a convencao do HTTPClient do Arduino: negativo = erro de
// conexao/timeout (sem resposta HTTP de verdade), positivo = status HTTP.
AcaoAposResposta decidirAcaoAposResposta(int codigo);

// Monta o corpo JSON de um lote (ate MAX_AMOSTRAS_POR_LOTE amostras) no
// formato exato que o backend espera -- ver §4. Escreve em `saida`
// (capacidade `capacidadeSaida`), devolve os bytes escritos, ou 0 se nao
// coube (chamador deve tratar como erro de programacao, nunca deveria
// acontecer com os tamanhos ja dimensionados).
size_t montarPayloadJson(const AmostraTelemetria* amostras, uint32_t total,
                          const char* deviceId, const char* firmwareVersion,
                          uint32_t uptimeSeconds, uint32_t freeHeapBytes,
                          int wifiRssiDbm,
                          char* saida, size_t capacidadeSaida);
```

### Por que copiar em vez de "enviar direto do buffer"

`copiarProximoLote` + `removerMaisAntigas` como dois passos separados (em vez
de uma função só "envia e remove") é o que permite a lógica de retry: o
chamador (na integração real) copia o lote, tenta o HTTP, e SÓ remove depois
de saber o resultado. Se a copia e a remoção fossem uma coisa só, não teria
como "desfazer" a remoção depois de uma falha de rede.

### Por que a montagem do JSON é lógica pura, não fica dentro da chamada HTTP

`ArduinoJson` é uma biblioteca header-only que não depende de `Arduino.h` —
compila em `env:native` igual `medicao.cpp`. Isolar `montarPayloadJson` aqui
significa que o formato exato do payload (nomes dos campos, estrutura
aninhada) tem prova automatizada, sem precisar de placa nem de rede.

---

## 4. Payload — mesmo contrato do `backend/README.md`, sem invenção

```json
{
  "device_id": "anemometro-01",
  "firmware_version": "1.0.0",
  "health": {
    "uptime_seconds": 86400,
    "free_heap_bytes": 351476,
    "wifi_rssi_dbm": -62
  },
  "samples": [
    { "measured_at": 1755432000, "avg_speed_ms": 3.21, "gust_speed_ms": 5.84 }
  ]
}
```

`health` é sempre o snapshot ATUAL no momento do envio (uptime, heap, RSSI
de agora) — não histórico, não por amostra. Isso já é assim no schema do
backend (`device_health` é upsert, uma linha por dispositivo).

---

## 5. Integração em `main.cpp` — a parte que toca rede de verdade

### Ciclo de esvaziamento (a cada 60 s, não-bloqueante, mesmo padrão `millis()`)

```
se NÃO wifiConectado(): pula este ciclo (dados continuam no buffer)
loteAtual = 0
enquanto quantidade(buffer) > 0 E loteAtual < MAX_LOTES_POR_CICLO:
    n = copiarProximoLote(buffer, saida, MAX_AMOSTRAS_POR_LOTE)
    payload = montarPayloadJson(saida, n, DEVICE_ID, FIRMWARE_VERSION,
                                 uptime, heapLivre, wifiRssiDbm())
    codigo = HTTPClient POST payload (timeout 8s)
    esp_task_wdt_reset()   // alimenta o watchdog entre lotes -- um
                            // esvaziamento de varios lotes nao pode
                            // disparar um reset por demorar mais que
                            // o timeout do watchdog
    acao = decidirAcaoAposResposta(codigo)
    se acao == Remover OU acao == Descartar:
        removerMaisAntigas(buffer, n)
        loteAtual++
        continua (ainda pode ter mais lotes)
    se acao == Manter:
        para o ciclo aqui -- tenta de novo no proximo (60s)
```

Não enviar antes do relógio (NTP) sincronizar: `inserirAmostra` só é chamada
depois de `horaAtualUnix()` confirmar sync — evita `measured_at` inválido,
que o backend já rejeitaria mesmo.

### Watchdog

`esp_task_wdt` nativo do ESP32. `iniciarWatchdog()` no `setup()` (timeout de
30 s — folga confortável sobre o pior caso realista de um único POST com
timeout de 8s + reconexão de Wi-Fi), `esp_task_wdt_reset()` a cada volta do
`loop()` **e** entre cada lote dentro do ciclo de esvaziamento (ver acima).

### OTA

`ArduinoOTA.h`, configurado no `setup()` (hostname, senha opcional vinda de
`secrets.h`), `ArduinoOTA.handle()` a cada volta do `loop()`. `platformio.ini`
ganha `upload_protocol = espota` num ambiente que estende `env:esp32dev`;
`Makefile` ganha uma forma de apontar `--upload-port <ip>` (reaproveitando o
mecanismo `PORT=` já existente).

---

## 6. Configuração

`include/secrets.h` (já gitignored) ganha:

```cpp
#define INGEST_TOKEN   "..."
#define INGEST_URL     "http://<ip-ou-host>:8000/api/v1/ingest"
#define OTA_SENHA      "..."  // opcional -- ArduinoOTA aceita sem senha
```

`device_id` e `firmware_version` são `constexpr` simples (não são segredo),
declarados junto de `telemetria.h` ou num header pequeno próprio:

```cpp
constexpr const char* DEVICE_ID         = "anemometro-01";
constexpr const char* FIRMWARE_VERSION  = "1.0.0";
```

---

## 7. Ajuste em `wifi_gerenciado.h`

Uma função nova, reaproveitando a sincronização NTP que a Fase 4 já faz:

```cpp
// Epoch UTC em segundos. So chamar depois de confirmar sync (ver
// horaAtualFormatada(), que ja indica "sincronizando" enquanto nao sincronizou).
time_t horaAtualUnix();

// Verdadeiro assim que o NTP sincronizou pela primeira vez desde o boot.
bool relogioSincronizado();
```

---

## 8. Testes

### Nativos (`env:native`)

- `inserirAmostra`: buffer com espaço sobrando grava normalmente
- `inserirAmostra` no buffer cheio: sobrescreve a mais antiga, incrementa
  `descartadasPorOverflow`, `quantidade` não passa de `CAPACIDADE_BUFFER_TELEMETRIA`
- `copiarProximoLote` com menos amostras que a capacidade pedida: copia só o
  que tem, devolve a contagem real
- `copiarProximoLote` com mais amostras que `MAX_AMOSTRAS_POR_LOTE`: copia
  exatamente o teto, mantém o resto no buffer
- `removerMaisAntigas` seguido de `inserirAmostra`: buffer circular não
  corrompe (índices voltam certo depois de dar a volta)
- `decidirAcaoAposResposta`: 200/201 → `Remover`; 400/422 → `Descartar`;
  500/503 → `Manter`; negativo (erro de conexão) → `Manter`
- `montarPayloadJson`: compara o JSON produzido, campo a campo, contra o
  exemplo do `backend/README.md` — nomes exatos, tipos exatos, `samples`
  como array mesmo com 1 elemento só
- `montarPayloadJson` com buffer de saída pequeno demais: devolve 0, não
  escreve fora dos limites (mesmo espírito do `gravarTimestampSeCouber` da
  Fase 2+3)

### No hardware real (não coberto pelos testes nativos)

`HTTPClient` de verdade contra o backend rodando, `ArduinoOTA` recebendo um
upload real, `esp_task_wdt` de fato reiniciando o dispositivo se o loop
travar (proposital, em bancada) — ficam para verificação manual ao vivo
durante a implementação, mesmo padrão da Fase 2+3 com a ISR.

---

## 9. Riscos conhecidos

| Risco | Mitigação |
|---|---|
| Buffer só em RAM — perde dados não enviados se o ESP32 reiniciar (queda de energia, watchdog, OTA) | Decisão deliberada (ver brainstorm): estação ligada na tomada, reinícios devem ser raros; simplicidade e ausência de desgaste de flash pesaram mais que a garantia de zero perda. Documentado aqui para revisitar se reinícios se mostrarem frequentes na prática. |
| Overflow do buffer descarta o **mais antigo** — uma queda de Wi-Fi muito longa (>4h) perde o início do período, não o fim | Decisão deliberada: para um painel de monitoramento, o vento mais recente é mais valioso que um recorte completo de um passado distante. `descartadasPorOverflow` fica visível no Serial. |
| ISR ainda não é totalmente cache-safe durante escrita de flash (`pendencias-hardware.md #5`) | Buffer de telemetria em RAM evita agravar esse risco — não há escrita de flash nova introduzida por esta fase. OTA em si grava flash (inerente ao mecanismo), mas é um evento raro e deliberado, não contínuo como seria um buffer em NVS. |
| `esp_task_wdt` disparando falso-positivo durante um esvaziamento de vários lotes seguidos | `esp_task_wdt_reset()` chamado entre cada lote do ciclo de esvaziamento, não só uma vez por volta do `loop()` |
| `ArduinoJson` como dependência nova | Biblioteca madura, padrão de fato do ecossistema Arduino/ESP32, header-only — sem custo de manutenção incomum |

---

## 10. Fora de escopo deste ciclo

- Persistência do buffer em NVS/flash — ver risco acima, decisão deliberada
- Contador de sequência no payload — o backend já resolve idempotência via
  `(device_id, measured_at)`, não precisa de sequência adicional
- HTTPS/TLS — backend roda HTTP puro na rede local hoje (ver brainstorm)
- Extensão do schema de `health` (ex.: enviar `descartadasPorOverflow` pro
  backend) — mudaria o contrato do lado do servidor, fora do escopo de um
  ciclo só de firmware
- Fase 6 (PC817, instalação física) — inalterado por esta fase
