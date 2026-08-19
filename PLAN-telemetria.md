# Telemetria (Fase 5) — Plano de Implementação

> **Para executores agênticos:** SUB-SKILL OBRIGATÓRIA: usar `superpowers:subagent-driven-development` (recomendado) ou `superpowers:executing-plans` para implementar tarefa a tarefa. Os passos usam checkbox (`- [ ]`) para rastreio.

**Goal:** Fazer cada `Amostra` que a Fase 2+3 já produz a cada 10s chegar no backend via `POST /api/v1/ingest`, em lotes de 60s, sobrevivendo a quedas de Wi-Fi via buffer em RAM — mais watchdog (reset automático se o loop travar) e OTA (atualização de firmware pela rede local).

**Architecture:** Mesma fronteira lógica-pura/hardware-real da Fase 2+3. `telemetria.{h,cpp}` é matemática/lógica pura (buffer circular, decisão de retry, montagem de JSON) — testável no Mac via `env:native`, inclusive a montagem do JSON (`ArduinoJson` não depende de `Arduino.h`). `envio.{h,cpp}` e `watchdog.{h,cpp}` tocam hardware/rede de verdade (`HTTPClient`, `esp_task_wdt`) — só compilam para `esp32dev`, verificados por compilação + revisão + teste ao vivo contra o backend real. `main.cpp` fica só como orquestração: chama as funções certas no ritmo certo.

**Tech Stack:** PlatformIO, C++ sobre Arduino core (ESP32), `ArduinoJson` (nova dependência), `HTTPClient`/`WiFiClient` (já no framework), `esp_task_wdt` (ESP-IDF, via Arduino core), `ArduinoOTA`.

**Spec:** [`specs/telemetria.md`](specs/telemetria.md) · Pré-requisitos: [`specs/medicao.md`](specs/medicao.md) (já implementado), `backend/README.md` no repo do servidor (contrato de ingestão)

## Global Constraints

- Buffer de telemetria: **1440 posições** (`CAPACIDADE_BUFFER_TELEMETRIA`, ~4h a 10s/amostra), só em RAM — perda em reinício é aceitável (decisão deliberada, ver spec §9)
- Overflow do buffer: descarta a amostra **mais antiga** (preserva o mais recente)
- Limite do backend por requisição: **500 amostras/lote** (`MAX_AMOSTRAS_POR_LOTE`)
- Teto de lotes por ciclo de esvaziamento: **3** (`MAX_LOTES_POR_CICLO` = ceil(1440/500))
- Ciclo de esvaziamento: a cada **60s**, não-bloqueante (`millis()`)
- Timeout HTTP por tentativa: **8000ms**
- Regra de retry (contrato do backend, `backend/README.md`): 2xx → remove do buffer; 4xx → descarta o lote e loga; erro de conexão ou 5xx → mantém no buffer, tenta no próximo ciclo
- Watchdog: **30s** de timeout, alimentado a cada volta do `loop()` **e** entre cada lote do ciclo de esvaziamento
- HTTP puro (sem TLS) — backend roda na rede local
- Não guardar amostra no buffer antes do relógio (NTP) confirmar sincronização
- `descartadasPorOverflow` é só diagnóstico local (Serial) — não entra no payload, schema do backend já está fechado
- Commits em português, conventional commits, SEM menção a IA e SEM Co-Authored-By
- `make <alvo> ENV=<ambiente>` já é o mecanismo existente do Makefile — não criar alvos novos redundantes; `PORT=<ip>` já funciona para OTA sem mudança no Makefile

---

## Estrutura de arquivos

| Arquivo | Responsabilidade |
|---|---|
| `platformio.ini` | Ganha `lib_deps` (ArduinoJson) em `esp32dev`+`native`, `telemetria.cpp` no `build_src_filter` do `native`, e `[env:ota]` |
| `include/telemetria.h` / `src/telemetria.cpp` | Lógica pura: buffer circular, decisão de retry, montagem do payload JSON — testável em `env:native` |
| `include/wifi_gerenciado.h` / `src/wifi_gerenciado.cpp` | Ganha `horaAtualUnix()` e `relogioSincronizado()` |
| `include/watchdog.h` / `src/watchdog.cpp` | `iniciarWatchdog()`, `alimentarWatchdog()` — `esp_task_wdt` |
| `include/envio.h` / `src/envio.cpp` | `registrarAmostra()`, `tentarEnviarLotes()` — HTTP de verdade, buffer de telemetria vive aqui |
| `include/secrets.h.example` | Ganha `INGEST_TOKEN`, `INGEST_URL`, `OTA_SENHA` |
| `src/main.cpp` | Orquestração: chama `registrarAmostra`/`tentarEnviarLotes`/`alimentarWatchdog`/`ArduinoOTA.handle()` nos ritmos certos |
| `README.md` | Documenta comandos novos, config, e atualiza Status/Estrutura (estavam desatualizados desde a Fase 2+3) |
| `test/test_telemetria/test_telemetria.cpp` | Testes nativos de `telemetria.cpp` |

---

## Task 1: Buffer circular de telemetria

**Files:**
- Create: `include/telemetria.h`
- Create: `src/telemetria.cpp`
- Create: `test/test_telemetria/test_telemetria.cpp`
- Modify: `platformio.ini`

**Interfaces:**
- Produces: `AmostraTelemetria`, `BufferTelemetria`, `inserirAmostra(BufferTelemetria&, const AmostraTelemetria&)`, `copiarProximoLote(const BufferTelemetria&, AmostraTelemetria*, uint32_t) -> uint32_t`, `removerMaisAntigas(BufferTelemetria&, uint32_t)`, constantes `CAPACIDADE_BUFFER_TELEMETRIA`, `MAX_AMOSTRAS_POR_LOTE`, `MAX_LOTES_POR_CICLO`

- [ ] **Step 1: Acrescentar `telemetria.cpp` ao build nativo**

Em `platformio.ini`, na seção `[env:native]`, troque a linha do `build_src_filter`:

```ini
build_src_filter = -<*> +<medicao.cpp> +<telemetria.cpp>
```

(era `-<*> +<medicao.cpp>` — só isso muda, mais nada na seção).

- [ ] **Step 2: Escrever os testes que falham**

`test/test_telemetria/test_telemetria.cpp`:

```cpp
#include <unity.h>

#include "telemetria.h"

void test_inserir_com_espaco_sobrando(void) {
    BufferTelemetria buffer = {};
    AmostraTelemetria amostra = {1000, 3.5f, 5.0f};

    inserirAmostra(buffer, amostra);

    TEST_ASSERT_EQUAL_UINT32(1, buffer.quantidade);
    TEST_ASSERT_EQUAL_UINT32(0, buffer.inicio);
    TEST_ASSERT_EQUAL_UINT32(1000, buffer.amostras[0].measuredAt);
}

void test_inserir_buffer_cheio_sobrescreve_mais_antiga(void) {
    BufferTelemetria buffer = {};
    for (uint32_t i = 0; i < CAPACIDADE_BUFFER_TELEMETRIA; i++) {
        AmostraTelemetria a = {i, (float) i, (float) i};
        inserirAmostra(buffer, a);
    }
    TEST_ASSERT_EQUAL_UINT32(CAPACIDADE_BUFFER_TELEMETRIA, buffer.quantidade);
    TEST_ASSERT_EQUAL_UINT32(0, buffer.descartadasPorOverflow);

    AmostraTelemetria nova = {99999, 7.0f, 9.0f};
    inserirAmostra(buffer, nova);

    TEST_ASSERT_EQUAL_UINT32(CAPACIDADE_BUFFER_TELEMETRIA, buffer.quantidade);
    TEST_ASSERT_EQUAL_UINT32(1, buffer.descartadasPorOverflow);

    // A mais antiga que sobra agora deve ser measuredAt=1 (a original
    // measuredAt=0 foi descartada para abrir espaco para a nova)
    AmostraTelemetria saida[1];
    copiarProximoLote(buffer, saida, 1);
    TEST_ASSERT_EQUAL_UINT32(1, saida[0].measuredAt);
}

void test_copiar_lote_menor_que_capacidade_pedida(void) {
    BufferTelemetria buffer = {};
    inserirAmostra(buffer, AmostraTelemetria{100, 1.0f, 2.0f});
    inserirAmostra(buffer, AmostraTelemetria{110, 1.5f, 2.5f});

    AmostraTelemetria saida[10];
    uint32_t copiadas = copiarProximoLote(buffer, saida, 10);

    TEST_ASSERT_EQUAL_UINT32(2, copiadas);
    TEST_ASSERT_EQUAL_UINT32(100, saida[0].measuredAt);
    TEST_ASSERT_EQUAL_UINT32(110, saida[1].measuredAt);
}

void test_copiar_lote_capado_no_maximo_pedido(void) {
    BufferTelemetria buffer = {};
    for (uint32_t i = 0; i < 5; i++) {
        inserirAmostra(buffer, AmostraTelemetria{100 + i, (float) i, (float) i});
    }

    AmostraTelemetria saida[3];
    uint32_t copiadas = copiarProximoLote(buffer, saida, 3);

    TEST_ASSERT_EQUAL_UINT32(3, copiadas);
    TEST_ASSERT_EQUAL_UINT32(100, saida[0].measuredAt);
    TEST_ASSERT_EQUAL_UINT32(102, saida[2].measuredAt);
    // copiar nao remove -- buffer inalterado
    TEST_ASSERT_EQUAL_UINT32(5, buffer.quantidade);
}

void test_remover_mais_antigas_depois_inserir_nao_corrompe_indices(void) {
    BufferTelemetria buffer = {};
    for (uint32_t i = 0; i < CAPACIDADE_BUFFER_TELEMETRIA; i++) {
        inserirAmostra(buffer, AmostraTelemetria{i, (float) i, (float) i});
    }

    removerMaisAntigas(buffer, CAPACIDADE_BUFFER_TELEMETRIA / 2);
    TEST_ASSERT_EQUAL_UINT32(CAPACIDADE_BUFFER_TELEMETRIA / 2, buffer.quantidade);

    for (uint32_t i = 0; i < 10; i++) {
        inserirAmostra(buffer, AmostraTelemetria{90000 + i, (float) i, (float) i});
    }
    TEST_ASSERT_EQUAL_UINT32(CAPACIDADE_BUFFER_TELEMETRIA / 2 + 10, buffer.quantidade);

    // A mais antiga que sobra deve ser measuredAt = CAPACIDADE/2 (a
    // primeira metade original foi removida)
    AmostraTelemetria saida[1];
    copiarProximoLote(buffer, saida, 1);
    TEST_ASSERT_EQUAL_UINT32(CAPACIDADE_BUFFER_TELEMETRIA / 2, saida[0].measuredAt);
}

void test_remover_mais_que_quantidade_disponivel_nao_estoura(void) {
    BufferTelemetria buffer = {};
    inserirAmostra(buffer, AmostraTelemetria{1, 1.0f, 1.0f});
    inserirAmostra(buffer, AmostraTelemetria{2, 2.0f, 2.0f});

    removerMaisAntigas(buffer, 100);

    TEST_ASSERT_EQUAL_UINT32(0, buffer.quantidade);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_inserir_com_espaco_sobrando);
    RUN_TEST(test_inserir_buffer_cheio_sobrescreve_mais_antiga);
    RUN_TEST(test_copiar_lote_menor_que_capacidade_pedida);
    RUN_TEST(test_copiar_lote_capado_no_maximo_pedido);
    RUN_TEST(test_remover_mais_antigas_depois_inserir_nao_corrompe_indices);
    RUN_TEST(test_remover_mais_que_quantidade_disponivel_nao_estoura);
    return UNITY_END();
}
```

- [ ] **Step 3: Rodar e confirmar que falha**

Run: `pio test -e native`
Expected: FAIL — `fatal error: 'telemetria.h' file not found`

- [ ] **Step 4: Implementar `include/telemetria.h`**

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

// ~4h de buffer a 10s/amostra. So em RAM -- decisao deliberada, ver
// specs/telemetria.md #9: perda de dados nao-enviados em reinicio e
// aceitavel para esta estacao (ligada na tomada, reinicios devem ser
// raros); simplicidade e ausencia de desgaste de flash pesaram mais.
constexpr uint32_t CAPACIDADE_BUFFER_TELEMETRIA = 1440;

// Limite do backend por requisicao (backend/README.md).
constexpr uint32_t MAX_AMOSTRAS_POR_LOTE = 500;

// Teto de lotes seguidos numa unica tentativa de esvaziamento. Dado
// CAPACIDADE_BUFFER_TELEMETRIA=1440 e MAX_AMOSTRAS_POR_LOTE=500, 3 lotes
// (ceil(1440/500)) ja bastam pra esvaziar o buffer inteiro numa unica
// passada -- existe como teto explicito pra nao depender de recalcular
// esse numero a mao se a capacidade do buffer mudar no futuro.
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
    uint32_t descartadasPorOverflow;  // contador so-diagnostico (Serial) --
                                        // nao entra no payload
};

// Insere uma amostra. Se o buffer estiver cheio, sobrescreve a mais
// antiga (preserva o mais recente) e incrementa descartadasPorOverflow.
void inserirAmostra(BufferTelemetria& buffer, const AmostraTelemetria& amostra);

// Copia ate `capacidadeSaida` das amostras mais antigas para `saida`,
// SEM remover do buffer -- quem chama decide se remove, depois de saber
// o resultado do envio. Devolve quantas copiou (pode ser menos que
// capacidadeSaida se o buffer tiver menos amostras que isso).
uint32_t copiarProximoLote(const BufferTelemetria& buffer,
                            AmostraTelemetria* saida, uint32_t capacidadeSaida);

// Remove as `n` amostras mais antigas -- chamado depois de confirmar que
// foram aceitas (2xx) ou definitivamente rejeitadas (4xx). Se `n` for
// maior que a quantidade disponivel, remove so o que tem (nunca da
// numero negativo).
void removerMaisAntigas(BufferTelemetria& buffer, uint32_t n);
```

- [ ] **Step 5: Implementar `src/telemetria.cpp`**

```cpp
#include "telemetria.h"

void inserirAmostra(BufferTelemetria& buffer, const AmostraTelemetria& amostra)
{
    if (buffer.quantidade < CAPACIDADE_BUFFER_TELEMETRIA) {
        const uint32_t posicao = (buffer.inicio + buffer.quantidade) % CAPACIDADE_BUFFER_TELEMETRIA;
        buffer.amostras[posicao] = amostra;
        buffer.quantidade++;
        return;
    }

    // Buffer cheio: sobrescreve a mais antiga (buffer.inicio) e avanca o
    // inicio -- preserva o mais recente, descarta o mais antigo.
    buffer.amostras[buffer.inicio] = amostra;
    buffer.inicio = (buffer.inicio + 1) % CAPACIDADE_BUFFER_TELEMETRIA;
    buffer.descartadasPorOverflow++;
}

uint32_t copiarProximoLote(const BufferTelemetria& buffer,
                            AmostraTelemetria* saida, uint32_t capacidadeSaida)
{
    const uint32_t n = buffer.quantidade < capacidadeSaida ? buffer.quantidade : capacidadeSaida;

    for (uint32_t i = 0; i < n; i++) {
        const uint32_t posicao = (buffer.inicio + i) % CAPACIDADE_BUFFER_TELEMETRIA;
        saida[i] = buffer.amostras[posicao];
    }

    return n;
}

void removerMaisAntigas(BufferTelemetria& buffer, uint32_t n)
{
    const uint32_t remover = n < buffer.quantidade ? n : buffer.quantidade;
    buffer.inicio = (buffer.inicio + remover) % CAPACIDADE_BUFFER_TELEMETRIA;
    buffer.quantidade -= remover;
}
```

- [ ] **Step 6: Rodar e confirmar que passa**

Run: `pio test -e native`
Expected: 6 testes, `0 Failures`

- [ ] **Step 7: Commit**

```bash
git add platformio.ini include/telemetria.h src/telemetria.cpp test/test_telemetria/test_telemetria.cpp
git commit -m "feat: buffer circular de telemetria, testado nativamente"
```

---

## Task 2: Decisão de retry por código HTTP

**Files:**
- Modify: `include/telemetria.h`
- Modify: `src/telemetria.cpp`
- Modify: `test/test_telemetria/test_telemetria.cpp`

**Interfaces:**
- Consumes: nada de Task 1
- Produces: `enum class AcaoAposResposta { Remover, Descartar, Manter }`, `decidirAcaoAposResposta(int) -> AcaoAposResposta`

- [ ] **Step 1: Escrever os testes que falham**

Acrescentar a `test/test_telemetria/test_telemetria.cpp`, antes do `main`:

```cpp
void test_acao_2xx_remove(void) {
    TEST_ASSERT_TRUE(AcaoAposResposta::Remover == decidirAcaoAposResposta(200));
    TEST_ASSERT_TRUE(AcaoAposResposta::Remover == decidirAcaoAposResposta(201));
}

void test_acao_4xx_descarta(void) {
    TEST_ASSERT_TRUE(AcaoAposResposta::Descartar == decidirAcaoAposResposta(400));
    TEST_ASSERT_TRUE(AcaoAposResposta::Descartar == decidirAcaoAposResposta(422));
}

void test_acao_5xx_mantem(void) {
    TEST_ASSERT_TRUE(AcaoAposResposta::Manter == decidirAcaoAposResposta(500));
    TEST_ASSERT_TRUE(AcaoAposResposta::Manter == decidirAcaoAposResposta(503));
}

void test_acao_erro_de_conexao_mantem(void) {
    // HTTPClient do Arduino devolve codigo negativo quando nao ha
    // resposta HTTP de verdade (timeout, recusa de conexao, etc.)
    TEST_ASSERT_TRUE(AcaoAposResposta::Manter == decidirAcaoAposResposta(-1));
    TEST_ASSERT_TRUE(AcaoAposResposta::Manter == decidirAcaoAposResposta(-11));
}
```

E os `RUN_TEST` correspondentes dentro de `main`:

```cpp
    RUN_TEST(test_acao_2xx_remove);
    RUN_TEST(test_acao_4xx_descarta);
    RUN_TEST(test_acao_5xx_mantem);
    RUN_TEST(test_acao_erro_de_conexao_mantem);
```

- [ ] **Step 2: Rodar e confirmar que falha**

Run: `pio test -e native`
Expected: FAIL — `AcaoAposResposta`/`decidirAcaoAposResposta` não declarados

- [ ] **Step 3: Acrescentar a `include/telemetria.h`**

```cpp
enum class AcaoAposResposta {
    Remover,    // 2xx -- lote aceito, remove do buffer
    Descartar,  // 4xx -- lote invalido (payload malformado, token errado,
                // lote grande demais) -- reenviar o mesmo lote so
                // repetiria o mesmo erro, entao descarta e loga
    Manter,     // erro de conexao/timeout, ou 5xx -- problema transitorio,
                // mantem no buffer para a proxima tentativa
};

// `codigo` segue a convencao do HTTPClient do Arduino: negativo = erro
// de conexao/timeout (sem resposta HTTP de verdade), positivo = status
// HTTP de verdade.
AcaoAposResposta decidirAcaoAposResposta(int codigo);
```

- [ ] **Step 4: Acrescentar a `src/telemetria.cpp`**

```cpp
AcaoAposResposta decidirAcaoAposResposta(int codigo)
{
    if (codigo < 0) {
        return AcaoAposResposta::Manter;
    }
    if (codigo >= 200 && codigo < 300) {
        return AcaoAposResposta::Remover;
    }
    if (codigo >= 400 && codigo < 500) {
        return AcaoAposResposta::Descartar;
    }
    return AcaoAposResposta::Manter;
}
```

- [ ] **Step 5: Rodar e confirmar que passa**

Run: `pio test -e native`
Expected: 10 testes, `0 Failures`

- [ ] **Step 6: Commit**

```bash
git add include/telemetria.h src/telemetria.cpp test/test_telemetria/test_telemetria.cpp
git commit -m "feat: decisao de retry por codigo HTTP, testada nativamente"
```

---

## Task 3: Montagem do payload JSON

**Files:**
- Modify: `include/telemetria.h`
- Modify: `src/telemetria.cpp`
- Modify: `test/test_telemetria/test_telemetria.cpp`
- Modify: `platformio.ini`

**Interfaces:**
- Consumes: `AmostraTelemetria` (Task 1)
- Produces: `montarPayloadJson(...) -> size_t`, constante `CAPACIDADE_PAYLOAD_JSON`

- [ ] **Step 1: Acrescentar a dependência do ArduinoJson**

Em `platformio.ini`, acrescentar `lib_deps` em **ambas** as seções `[env:esp32dev]` e `[env:native]` (a montagem do payload roda nos dois — no ESP32 de verdade e nos testes nativos):

```ini
lib_deps = bblanchon/ArduinoJson@^7.0.0
```

`[env:calibracao]` herda automaticamente via `extends = env:esp32dev`, não precisa de linha própria.

- [ ] **Step 2: Escrever os testes que falham**

Acrescentar a `test/test_telemetria/test_telemetria.cpp`:

```cpp
#include <ArduinoJson.h>

void test_montar_payload_estrutura_correta(void) {
    AmostraTelemetria amostras[2] = {
        {1755432000, 3.21f, 5.84f},
        {1755432010, 3.44f, 4.10f},
    };

    char saida[512];
    size_t escrito = montarPayloadJson(amostras, 2, "anemometro-01", "1.0.0",
                                        86400, 351476, -62, saida, sizeof(saida));

    TEST_ASSERT_TRUE(escrito > 0);

    JsonDocument doc;
    DeserializationError erro = deserializeJson(doc, saida, escrito);
    TEST_ASSERT_TRUE(erro == DeserializationError::Ok);

    TEST_ASSERT_EQUAL_STRING("anemometro-01", doc["device_id"]);
    TEST_ASSERT_EQUAL_STRING("1.0.0", doc["firmware_version"]);
    TEST_ASSERT_EQUAL_UINT32(86400, doc["health"]["uptime_seconds"]);
    TEST_ASSERT_EQUAL_UINT32(351476, doc["health"]["free_heap_bytes"]);
    TEST_ASSERT_EQUAL_INT(-62, doc["health"]["wifi_rssi_dbm"]);

    TEST_ASSERT_EQUAL_UINT32(2, doc["samples"].size());
    TEST_ASSERT_EQUAL_UINT32(1755432000, doc["samples"][0]["measured_at"]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.21f, doc["samples"][0]["avg_speed_ms"]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.84f, doc["samples"][0]["gust_speed_ms"]);
    TEST_ASSERT_EQUAL_UINT32(1755432010, doc["samples"][1]["measured_at"]);
}

void test_montar_payload_buffer_pequeno_demais_devolve_zero(void) {
    AmostraTelemetria amostras[1] = {{1000, 1.0f, 2.0f}};

    char saidaMinuscula[5];  // com certeza pequeno demais
    size_t escrito = montarPayloadJson(amostras, 1, "anemometro-01", "1.0.0",
                                        100, 200000, -50, saidaMinuscula, sizeof(saidaMinuscula));

    TEST_ASSERT_EQUAL_UINT32(0, escrito);
}

void test_montar_payload_500_amostras_pior_caso_cabe_no_buffer(void) {
    // Pior caso de tamanho: 500 amostras (o maximo por lote) com o maior
    // numero de digitos plausivel (timestamp de 10 digitos, velocidades
    // negativas com sinal -- nunca acontece de verdade, mas testa a
    // string mais longa que o formato permite). Prova que
    // CAPACIDADE_PAYLOAD_JSON realmente comporta o pior caso, em vez de
    // confiar numa conta feita a mao.
    static AmostraTelemetria amostras[MAX_AMOSTRAS_POR_LOTE];
    for (uint32_t i = 0; i < MAX_AMOSTRAS_POR_LOTE; i++) {
        amostras[i] = AmostraTelemetria{2000000000u + i, -12.345678f, -37.500000f};
    }

    static char saida[CAPACIDADE_PAYLOAD_JSON];
    size_t escrito = montarPayloadJson(amostras, MAX_AMOSTRAS_POR_LOTE,
                                        "anemometro-01", "1.0.0",
                                        999999999, 999999999, -100,
                                        saida, sizeof(saida));

    TEST_ASSERT_TRUE(escrito > 0);
    TEST_ASSERT_TRUE(escrito < CAPACIDADE_PAYLOAD_JSON);
}
```

E os `RUN_TEST`:

```cpp
    RUN_TEST(test_montar_payload_estrutura_correta);
    RUN_TEST(test_montar_payload_buffer_pequeno_demais_devolve_zero);
    RUN_TEST(test_montar_payload_500_amostras_pior_caso_cabe_no_buffer);
```

- [ ] **Step 3: Rodar e confirmar que falha**

Run: `pio test -e native`
Expected: FAIL — `montarPayloadJson`/`CAPACIDADE_PAYLOAD_JSON` não declarados (ou erro de include se o `lib_deps` do Step 1 não tiver sido baixado ainda — rode `pio pkg install -e native` se precisar)

- [ ] **Step 4: Acrescentar a `include/telemetria.h`**

```cpp
// Pior caso: 500 amostras (MAX_AMOSTRAS_POR_LOTE) de ate ~82 bytes cada
// em JSON (~41KB) + cabecalho (device_id, firmware_version, health,
// ~250 bytes) -- com folga. O teste
// test_montar_payload_500_amostras_pior_caso_cabe_no_buffer prova isso
// de verdade, em vez de confiar só nesta conta.
constexpr size_t CAPACIDADE_PAYLOAD_JSON = 49152;

// Monta o corpo JSON de um lote (ate MAX_AMOSTRAS_POR_LOTE amostras) no
// formato exato que o backend espera (backend/README.md). Escreve em
// `saida` (capacidade `capacidadeSaida`), devolve os bytes escritos, ou
// 0 se nao coube -- chamador deve tratar como erro (nao deveria
// acontecer com CAPACIDADE_PAYLOAD_JSON, ver teste de pior caso).
size_t montarPayloadJson(const AmostraTelemetria* amostras, uint32_t total,
                          const char* deviceId, const char* firmwareVersion,
                          uint32_t uptimeSeconds, uint32_t freeHeapBytes,
                          int wifiRssiDbm,
                          char* saida, size_t capacidadeSaida);
```

- [ ] **Step 5: Acrescentar a `src/telemetria.cpp`**

```cpp
#include <ArduinoJson.h>

size_t montarPayloadJson(const AmostraTelemetria* amostras, uint32_t total,
                          const char* deviceId, const char* firmwareVersion,
                          uint32_t uptimeSeconds, uint32_t freeHeapBytes,
                          int wifiRssiDbm,
                          char* saida, size_t capacidadeSaida)
{
    JsonDocument doc;

    doc["device_id"] = deviceId;
    doc["firmware_version"] = firmwareVersion;

    JsonObject health = doc["health"].to<JsonObject>();
    health["uptime_seconds"] = uptimeSeconds;
    health["free_heap_bytes"] = freeHeapBytes;
    health["wifi_rssi_dbm"] = wifiRssiDbm;

    JsonArray samples = doc["samples"].to<JsonArray>();
    for (uint32_t i = 0; i < total; i++) {
        JsonObject amostra = samples.add<JsonObject>();
        amostra["measured_at"] = amostras[i].measuredAt;
        amostra["avg_speed_ms"] = amostras[i].avgSpeedMs;
        amostra["gust_speed_ms"] = amostras[i].gustSpeedMs;
    }

    const size_t escrito = serializeJson(doc, saida, capacidadeSaida);
    if (escrito == 0 || escrito >= capacidadeSaida) {
        return 0;
    }
    return escrito;
}
```

**Nota:** se a versão do `ArduinoJson` que o `pio pkg install` baixar tiver uma API diferente da mostrada aqui (v6 usa `createNestedObject()`/`createNestedArray()` em vez de `.to<JsonObject>()`/`.to<JsonArray>()` — a sintaxe mudou entre v6 e v7), ajuste as chamadas para o que compilar. O que importa é o comportamento — provado pelos testes do Step 2, não pela sintaxe exata mostrada aqui.

- [ ] **Step 6: Rodar e confirmar que passa**

Run: `pio test -e native`
Expected: 13 testes, `0 Failures`

- [ ] **Step 7: Commit**

```bash
git add platformio.ini include/telemetria.h src/telemetria.cpp test/test_telemetria/test_telemetria.cpp
git commit -m "feat: montagem do payload JSON de telemetria, testada nativamente"
```

---

## Task 4: Relógio em unix time (`wifi_gerenciado`)

**Files:**
- Modify: `include/wifi_gerenciado.h`
- Modify: `src/wifi_gerenciado.cpp`

**Interfaces:**
- Consumes: nada de tasks anteriores
- Produces: `time_t horaAtualUnix()`, `bool relogioSincronizado()`

⚠️ **Sem teste automatizado** — depende de `getLocalTime()`/`time()` do Arduino core, só compila para `esp32dev`. Verificação por compilação + revisão + confirmação ao vivo no monitor serial.

- [ ] **Step 1: Ler `src/wifi_gerenciado.cpp` e `include/wifi_gerenciado.h` atuais antes de editar**

Use a ferramenta de leitura para ver o conteúdo exato — o arquivo já tem `iniciarWifi()`, `atualizarWifi()`, `wifiConectado()`, `wifiRssiDbm()`, `wifiIp()`, `horaAtualFormatada()` funcionando desde a Fase 4, e não deve ser reescrito do zero, só estendido.

**Ponto crítico de correção, leia com atenção:** o projeto já chama `configTime(FUSO_SEGUNDOS, 0, SERVIDOR_NTP)` (com `FUSO_SEGUNDOS = -3*3600`, horário de Campinas). Isso configura `getLocalTime()`/`localtime()` para devolver a hora **local** (Campinas) num `struct tm`. **NÃO** use `mktime()` sobre esse `struct tm` para calcular o epoch UTC — `mktime()` assume que o `struct tm` está no fuso horário do sistema (que por padrão nesse ambiente é UTC, já que nunca é configurado via `setenv("TZ", ...)`), então aplicar `mktime()` sobre um `struct tm` que já está em horário LOCAL (Campinas, UTC-3) produziria um epoch **errado, adiantado em 3 horas**.

A forma correta: `configTime()` mantém o relógio interno do ESP32 (`time()`) sempre em UTC de verdade — o deslocamento de fuso só é aplicado quando `getLocalTime()`/`localtime()` convertem esse UTC interno para um `struct tm` de exibição. Ou seja, `time(nullptr)` já devolve o epoch UTC certo diretamente, sem precisar de `mktime()` nem de nenhuma conta de fuso horário.

- [ ] **Step 2: Acrescentar a `include/wifi_gerenciado.h`**

Se o arquivo ainda não incluir `<ctime>` (confirme lendo o Step 1), acrescente ao topo, junto dos includes existentes:

```cpp
#include <ctime>
```

E as duas declarações novas, junto das existentes (`wifiConectado`, `wifiRssiDbm`, etc.):

```cpp
// Verdadeiro assim que o NTP sincronizou pela primeira vez desde o
// boot -- mesmo mecanismo que horaAtualFormatada() ja usa (getLocalTime
// com timeout curto, nunca bloqueia o loop() esperando).
bool relogioSincronizado();

// Epoch UTC em segundos. Devolve 0 se o relogio ainda nao sincronizou --
// quem chama deve checar relogioSincronizado() antes de usar o valor
// para algo que importa (nunca grave um measured_at baseado num retorno
// de 0, o backend rejeitaria mesmo).
time_t horaAtualUnix();
```

- [ ] **Step 3: Acrescentar a `src/wifi_gerenciado.cpp`**

Logo depois de `horaAtualFormatada()`, no final do arquivo (mesmo estilo das funções existentes):

```cpp
bool relogioSincronizado()
{
    struct tm horario;
    return getLocalTime(&horario, 100);
}

time_t horaAtualUnix()
{
    if (!relogioSincronizado()) {
        return 0;
    }

    // time() le o relogio interno do ESP32, que configTime() mantem em
    // UTC de verdade -- SEM aplicar o deslocamento de fuso horario
    // (esse deslocamento so entra na conversao para struct tm, feita
    // por getLocalTime()/localtime(), nunca aqui). Por isso este valor
    // ja e o epoch UTC certo, sem precisar de mktime() nem de conta de
    // fuso horario.
    return time(nullptr);
}
```

- [ ] **Step 4: Compilar para o ESP32 real**

Run: `pio run -e esp32dev`
Expected: `SUCCESS`

- [ ] **Step 5: Confirmar que os testes nativos continuam passando**

Run: `pio test -e native`
Expected: 13 testes, `0 Failures` — `wifi_gerenciado.cpp` não é compilado em `env:native` (fora do `build_src_filter`), então isso só confirma que nada quebrou por engano

- [ ] **Step 6: Commit**

```bash
git add include/wifi_gerenciado.h src/wifi_gerenciado.cpp
git commit -m "feat: relogio em unix time, sem passar por mktime"
```

---

## Task 5: Watchdog

**Files:**
- Create: `include/watchdog.h`
- Create: `src/watchdog.cpp`

**Interfaces:**
- Consumes: nada de tasks anteriores
- Produces: `iniciarWatchdog()`, `alimentarWatchdog()`

⚠️ **Sem teste automatizado** — `esp_task_wdt` é específico do ESP-IDF, só compila para `esp32dev`. Verificação por compilação + revisão de código; o comportamento de reset de verdade fica para um teste manual proposital em bancada (opcional, ver Step 4).

- [ ] **Step 1: Implementar `include/watchdog.h`**

```cpp
#pragma once

// Task Watchdog Timer nativo do ESP32. Se o loop() nao alimentar o
// watchdog dentro do timeout, o dispositivo reinicia sozinho -- melhor
// um reboot visivel (aparece nos logs de uptime) do que ficar travado
// no topo da caixa d'agua sem ninguem notar.
//
// Chame iniciarWatchdog() uma vez no setup(). Chame alimentarWatchdog()
// a cada volta do loop() -- e tambem entre cada lote HTTP de um
// esvaziamento de telemetria com varios lotes seguidos, para um
// esvaziamento demorado nao disparar um reset por conta propria.
void iniciarWatchdog();
void alimentarWatchdog();
```

- [ ] **Step 2: Implementar `src/watchdog.cpp`**

```cpp
#include "watchdog.h"

#include <esp_task_wdt.h>

namespace {
// Folga confortavel sobre o pior caso realista de uma unica volta do
// loop(): um POST com timeout de 8s (ver envio.cpp), mais o tempo de
// uma reconexao de Wi-Fi (nao-bloqueante, mas pode levar alguns ciclos).
// O watchdog e alimentado entre cada lote de um esvaziamento com varios
// lotes (ver Task 6), entao o timeout aqui protege contra loop() travado
// de verdade, nao contra um envio HTTP legitimamente lento.
constexpr uint32_t TIMEOUT_SEGUNDOS = 30;
}  // namespace

void iniciarWatchdog()
{
    esp_task_wdt_init(TIMEOUT_SEGUNDOS, true);  // true = reinicia o ESP32 no timeout
    esp_task_wdt_add(NULL);  // registra a task atual (loop() do Arduino)
}

void alimentarWatchdog()
{
    esp_task_wdt_reset();
}
```

**Nota de compilação:** se `esp_task_wdt_init` reclamar de assinatura (versões mais novas do ESP-IDF trocaram para `esp_task_wdt_init(const esp_task_wdt_config_t*)`, uma struct de config em vez de dois parâmetros soltos), ajuste para a assinatura que o SDK instalado espera — o `platform = espressif32@^6.9.0` já fixado no projeto usa uma versão de ESP-IDF onde a assinatura de dois parâmetros (`timeout_s`, `panic`) é a correta; se isso mudar, documente a diferença no commit.

- [ ] **Step 3: Compilar para o ESP32 real**

Run: `pio run -e esp32dev`
Expected: `SUCCESS`

- [ ] **Step 4 (opcional, mas recomendado se o dispositivo estiver disponível): confirmar o reset de verdade em bancada**

Grave o firmware, e proposital-mente trave o `loop()` por mais de 30s (ex.: acrescente um `while(true) {}` temporário logo após `iniciarWatchdog()` no `setup()`, só para este teste manual — reverta antes de commitar). Observe no monitor serial que o ESP32 reinicia sozinho por volta de 30s depois. Reverta a trava antes do commit do Step 5.

- [ ] **Step 5: Commit**

```bash
git add include/watchdog.h src/watchdog.cpp
git commit -m "feat: watchdog nativo do ESP32, reinicia sozinho se o loop travar"
```

---

## Task 6: Envio HTTP + integração no `main.cpp`

**Files:**
- Create: `include/envio.h`
- Create: `src/envio.cpp`
- Modify: `src/main.cpp`
- Modify: `include/secrets.h.example`

**Interfaces:**
- Consumes: `Amostra` (medicao.h, Task anterior à Fase 5), `AmostraTelemetria`/`BufferTelemetria`/`inserirAmostra`/`copiarProximoLote`/`removerMaisAntigas`/`AcaoAposResposta`/`decidirAcaoAposResposta`/`montarPayloadJson`/`MAX_AMOSTRAS_POR_LOTE`/`MAX_LOTES_POR_CICLO`/`CAPACIDADE_PAYLOAD_JSON` (Tasks 1-3), `horaAtualUnix`/`relogioSincronizado` (Task 4), `alimentarWatchdog` (Task 5)
- Produces: `registrarAmostra(const Amostra&)`, `tentarEnviarLotes()`

⚠️ **Sem teste automatizado** — `HTTPClient` é específico do Arduino core, só compila para `esp32dev`. Verificação por compilação + revisão + **teste ao vivo contra o backend real rodando** (o backend já está implementado e pode rodar localmente — ver `backend/README.md` no repo do servidor, `uvicorn app.main:create_app --factory --reload`).

- [ ] **Step 1: Ler `src/main.cpp` atual antes de editar**

Use a ferramenta de leitura para ver o conteúdo exato — o arquivo já tem o caminho normal (Wi-Fi + medição a cada 10s) e o caminho `MODO_CALIBRACAO` separado por `#ifdef`. Você vai ESTENDER o caminho normal, sem tocar no caminho de calibração.

- [ ] **Step 2: Acrescentar a `include/secrets.h.example`**

```cpp
#define INGEST_TOKEN  "TROQUE_PELO_TOKEN_DO_BACKEND"
#define INGEST_URL    "http://192.168.0.100:8000/api/v1/ingest"
```

(o IP de exemplo é só placeholder — cada instalação edita o próprio `secrets.h`, nunca commitado)

- [ ] **Step 3: Implementar `include/envio.h`**

```cpp
#pragma once

#include "medicao.h"

// Converte a Amostra (ja calculada pela Fase 2+3) num registro de
// telemetria e guarda no buffer -- so se o relogio (NTP) ja estiver
// sincronizado, para nunca guardar measured_at invalido (o backend
// rejeitaria mesmo).
void registrarAmostra(const Amostra& amostra);

// Tenta esvaziar o buffer de telemetria via POST /api/v1/ingest, em ate
// MAX_LOTES_POR_CICLO lotes. So faz alguma coisa se o Wi-Fi estiver
// conectado -- senao, os dados continuam acumulando no buffer para a
// proxima tentativa.
void tentarEnviarLotes();
```

- [ ] **Step 4: Implementar `src/envio.cpp`**

```cpp
#include "envio.h"

#include <Arduino.h>
#include <HTTPClient.h>

#include "secrets.h"
#include "telemetria.h"
#include "watchdog.h"
#include "wifi_gerenciado.h"

namespace {

constexpr const char* DEVICE_ID        = "anemometro-01";
constexpr const char* FIRMWARE_VERSION = "1.0.0";
constexpr uint32_t    TIMEOUT_HTTP_MS  = 8000;

BufferTelemetria bufferTelemetria;
uint32_t         ultimoDescartadasReportado = 0;

}  // namespace

void registrarAmostra(const Amostra& amostra)
{
    if (!relogioSincronizado()) {
        Serial.println("[telemetria] relogio ainda nao sincronizado, amostra nao guardada");
        return;
    }

    const AmostraTelemetria registro = {
        (uint32_t) horaAtualUnix(), amostra.avgSpeedMs, amostra.gustSpeedMs
    };
    inserirAmostra(bufferTelemetria, registro);
}

void tentarEnviarLotes()
{
    if (!wifiConectado()) {
        return;
    }

    static char             payloadJson[CAPACIDADE_PAYLOAD_JSON];
    static AmostraTelemetria loteSaida[MAX_AMOSTRAS_POR_LOTE];

    uint32_t lotesEnviados = 0;
    while (bufferTelemetria.quantidade > 0 && lotesEnviados < MAX_LOTES_POR_CICLO) {
        const uint32_t n = copiarProximoLote(bufferTelemetria, loteSaida, MAX_AMOSTRAS_POR_LOTE);

        const size_t escrito = montarPayloadJson(
            loteSaida, n, DEVICE_ID, FIRMWARE_VERSION,
            (uint32_t) (millis() / 1000), ESP.getFreeHeap(), wifiRssiDbm(),
            payloadJson, sizeof(payloadJson));

        if (escrito == 0) {
            // Nao deveria acontecer com CAPACIDADE_PAYLOAD_JSON
            // dimensionado corretamente (ver teste de pior caso em
            // test_telemetria.cpp) -- mas se acontecer, falha alto em
            // vez de mandar payload truncado.
            Serial.println("[telemetria] ERRO: payload nao coube no buffer, lote NAO enviado");
            break;
        }

        HTTPClient http;
        http.begin(INGEST_URL);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", String("Bearer ") + INGEST_TOKEN);
        http.setTimeout(TIMEOUT_HTTP_MS);

        const int codigo = http.POST((uint8_t*) payloadJson, escrito);
        http.end();

        // Entre cada lote -- um esvaziamento de varios lotes seguidos
        // nao pode disparar o watchdog por demorar mais que o timeout
        // dele.
        alimentarWatchdog();

        const AcaoAposResposta acao = decidirAcaoAposResposta(codigo);

        if (acao == AcaoAposResposta::Manter) {
            Serial.printf("[telemetria] envio falhou (codigo=%d), mantendo %lu amostras no buffer\n",
                          codigo, (unsigned long) n);
            break;
        }

        if (acao == AcaoAposResposta::Descartar) {
            Serial.printf("[telemetria] lote rejeitado (codigo=%d), descartando %lu amostras\n",
                          codigo, (unsigned long) n);
        }

        removerMaisAntigas(bufferTelemetria, n);
        lotesEnviados++;
    }

    if (bufferTelemetria.descartadasPorOverflow != ultimoDescartadasReportado) {
        Serial.printf("[telemetria] descartadas_por_overflow=%lu (total desde o boot)\n",
                      (unsigned long) bufferTelemetria.descartadasPorOverflow);
        ultimoDescartadasReportado = bufferTelemetria.descartadasPorOverflow;
    }
}
```

- [ ] **Step 5: Integrar em `src/main.cpp`**

No topo, junto dos includes existentes (dentro da seção comum, não dentro do `#ifdef MODO_CALIBRACAO`):

```cpp
#include "envio.h"
#include "watchdog.h"
```

No `setup()` do caminho normal (fora do `#ifdef MODO_CALIBRACAO`), logo após `iniciarAnemometro()`:

```cpp
    iniciarWatchdog();
```

No topo do `loop()` do caminho normal, antes de `atualizarWifi()`:

```cpp
    alimentarWatchdog();
```

Dentro do bloco de medição de 10s já existente, logo depois do `if (janela.descartadosPorBuffer > 0) { ... }`:

```cpp
        registrarAmostra(amostra);
```

E um novo bloco de 60s, em qualquer ponto do `loop()` antes do `if (agora - ultimoStatus < INTERVALO_STATUS_MS) { return; }` final (mesmo motivo de sempre: não pode ficar atrás do `return` do status, que é um timer independente):

```cpp
    static uint32_t ultimoEnvio = 0;
    if (agora - ultimoEnvio >= 60000) {
        ultimoEnvio = agora;
        tentarEnviarLotes();
    }
```

Atualize também o comentário de cabeçalho do arquivo (linhas 1-9), que hoje descreve só Fase 4 + medição — acrescente uma linha mencionando o envio de telemetria da Fase 5.

- [ ] **Step 6: Compilar os dois ambientes**

Run: `pio run -e esp32dev`
Expected: `SUCCESS`

Run: `pio run -e calibracao`
Expected: `SUCCESS` (o caminho de calibração não chama nada de `envio.h`/`watchdog.h`, mas os arquivos precisam compilar/linkar mesmo assim, já que fazem parte de `src/`)

- [ ] **Step 7: Confirmar que os testes nativos continuam passando**

Run: `pio test -e native`
Expected: 13 testes, `0 Failures`

- [ ] **Step 8: Teste ao vivo contra o backend real**

Pré-requisito: backend rodando localmente (outro repositório, `ventos-cps`/`ventos-campinas`):

```bash
INGEST_TOKEN=segredo DATABASE_PATH=/tmp/ventos-telemetria-teste.db \
  uvicorn app.main:create_app --factory --reload
```

Configure `include/secrets.h` com `INGEST_TOKEN=segredo` e `INGEST_URL` apontando para o IP da máquina que está rodando o backend (não `localhost` — o ESP32 é outro dispositivo na rede), porta 8000.

Grave o firmware normal (`pio run -e esp32dev -t upload`), leia o monitor serial por pelo menos 90s (tempo suficiente para pelo menos uma janela de 60s de envio). Sem sensor conectado (`avg=0.00 gust=0.00`), confirme:

- Depois do NTP sincronizar, `[medicao]` continua aparecendo a cada 10s (sem regressão)
- Depois de ~60s, nenhuma linha de erro de `[telemetria]` — o envio deveria funcionar (mesmo com velocidade zero, é um payload válido)
- No terminal do backend (ou consultando `GET /api/v1/wind/current` via `curl`), confirme que os dados chegaram: `curl http://localhost:8000/api/v1/wind/current` deve devolver a última amostra com `avg_speed_ms: 0` e um `measured_at` recente

Se possível, teste também o cenário de falha: pare o backend por um tempo, confirme no monitor serial que aparece `[telemetria] envio falhou`, depois suba o backend de novo e confirme que o buffer acumulado é enviado no próximo ciclo de 60s.

Ao terminar o teste, encerre o processo do backend (`uvicorn`) e o `make monitor`/`pio device monitor` que você abriu — não deixe nenhum dos dois rodando em segundo plano depois deste step.

- [ ] **Step 9: Commit**

```bash
git add include/envio.h src/envio.cpp src/main.cpp include/secrets.h.example
git commit -m "feat: envio HTTP de telemetria em lote, integrado ao loop principal"
```

---

## Task 7: OTA + documentação

**Files:**
- Modify: `platformio.ini`
- Modify: `src/main.cpp`
- Modify: `include/secrets.h.example`
- Modify: `README.md`

**Interfaces:**
- Consumes: nada de código das tasks anteriores (é uma integração paralela, não depende de telemetria/envio/watchdog)

⚠️ **Sem teste automatizado** — `ArduinoOTA` é específico do Arduino core, só compila para `esp32dev`. Verificação por compilação + teste ao vivo de uma atualização real pela rede (Step 5).

- [ ] **Step 1: Acrescentar o ambiente de OTA ao `platformio.ini`**

No final do arquivo:

```ini
; --- Atualizacao via rede (OTA) ------------------------------
; make upload ENV=ota PORT=<ip-do-esp32-na-rede>
[env:ota]
extends = env:esp32dev
upload_protocol = espota
```

- [ ] **Step 2: Acrescentar a `include/secrets.h.example`**

```cpp
#define OTA_SENHA  "TROQUE_POR_UMA_SENHA_OTA"
```

- [ ] **Step 3: Integrar `ArduinoOTA` em `src/main.cpp`**

No topo, junto dos includes existentes:

```cpp
#include <ArduinoOTA.h>

#include "secrets.h"
```

(`main.cpp` ainda não inclui `secrets.h` diretamente — só `envio.cpp` inclui, desde a Task 6 — mas `OTA_SENHA` é usado aqui, direto no `setup()`, então precisa do include próprio.)

No `setup()` do caminho normal, logo após `iniciarWatchdog()`:

```cpp
    ArduinoOTA.setHostname("ventos-cps-anemometro");
    ArduinoOTA.setPassword(OTA_SENHA);
    ArduinoOTA.begin();
    Serial.println("[ota] pronto para atualizacao via rede");
```

No topo do `loop()` do caminho normal, logo após `alimentarWatchdog()`:

```cpp
    ArduinoOTA.handle();
```

- [ ] **Step 4: Compilar para o ESP32 real**

Run: `pio run -e esp32dev`
Expected: `SUCCESS`

- [ ] **Step 5: Teste ao vivo de uma atualização OTA real**

Grave o firmware normal via USB primeiro (`pio run -e esp32dev -t upload`), confirme que conecta ao Wi-Fi e mostra o IP no monitor serial. Depois, com o dispositivo já rodando, faça uma alteração trivial e visível (ex.: mude o texto de `"=== Ventos Campinas | Fase 4 + ... ==="` para incluir "Fase 5"), e grave via rede:

```bash
make upload ENV=ota PORT=<ip-mostrado-no-monitor-serial>
```

Confirme que o upload completa sem estar conectado por USB, e que o dispositivo reinicia rodando o firmware atualizado (a nova linha de texto aparece no boot seguinte via `make monitor`).

- [ ] **Step 6: Atualizar `README.md`**

Na tabela de comandos (perto de `make flash ENV=calibracao`), acrescentar:

```markdown
| `make upload ENV=ota PORT=<ip>` | Grava por Wi-Fi, sem cabo USB (dispositivo já rodando na rede) |
```

Na seção "Credenciais de Wi-Fi" (depois de explicar `secrets.h`), acrescentar uma seção nova:

```markdown
---

## Telemetria e OTA

Além do SSID/senha, `include/secrets.h` também define:

| Constante | Para quê |
|---|---|
| `INGEST_TOKEN` | Token do backend (`Authorization: Bearer`) — mesmo valor de `INGEST_TOKEN` na configuração do servidor |
| `INGEST_URL` | URL completa do endpoint de ingestão (`http://<ip-do-backend>:8000/api/v1/ingest`) |
| `OTA_SENHA` | Senha exigida para gravar por Wi-Fi (`make upload ENV=ota`) |
```

Substituir a seção `## Estrutura` inteira (está desatualizada desde a Fase 2+3 — nunca foi revisada depois que `anemometro.{h,cpp}` e `medicao.{h,cpp}` entraram) por:

```markdown
## Estrutura

\`\`\`
ventos-cps-firmware/
├── CLAUDE.md              convenções e forma de trabalho
├── Makefile               atalhos do PlatformIO
├── platformio.ini         configuração do build
├── include/
│   ├── secrets.h.example  template de credenciais (versionado)
│   ├── secrets.h          credenciais reais (git-ignored, você cria)
│   ├── wifi_gerenciado.h  Wi-Fi, NTP, relógio em unix time
│   ├── anemometro.h       ISR, GPIO — captura de pulsos
│   ├── medicao.h          matemática pura — velocidade, rajada, calmaria
│   ├── telemetria.h       lógica pura — buffer, retry, payload JSON
│   ├── envio.h            HTTP de verdade — envia telemetria pro backend
│   └── watchdog.h         reset automático se o loop travar
├── src/
│   ├── main.cpp           orquestração: setup(), loop()
│   ├── wifi_gerenciado.cpp
│   ├── anemometro.cpp
│   ├── medicao.cpp
│   ├── telemetria.cpp
│   ├── envio.cpp
│   └── watchdog.cpp
├── test/
│   ├── test_medicao/      testes nativos de medicao.cpp
│   └── test_telemetria/   testes nativos de telemetria.cpp
└── specs/                 hardware, requisitos de firmware, pendências
\`\`\`
```

Substituir a seção `## Status` inteira por:

```markdown
## Status

**Fases 1, 2, 3, 4 e 5 concluídas.**

- **Fase 1** — ambiente, compilação, gravação e monitor validados.
- **Fase 2+3** — captura de pulsos via ISR (GPIO 25) e cálculo de velocidade/rajada/calmaria, com matemática testada nativamente. Verificação da ISR sob interrupção real (pulsos/volta, debounce) segue pendente do conector dupont — checklist em [`specs/pendencias-hardware.md`](specs/pendencias-hardware.md).
- **Fase 4** — Wi-Fi não-bloqueante, reconexão com backoff exponencial, hora via NTP. Testada ao vivo. Sinal fraco medido perto do roteador (-82 a -84 dBm), relevante para a instalação da Fase 6.
- **Fase 5** — envio HTTP em lote (buffer de 4h em RAM), watchdog, OTA. Testada ao vivo contra o backend real.

**Fase 6 (instalação física, PC817)** — ainda não iniciada.

Roadmap completo: [`specs/README.md`](specs/README.md#roadmap).
```

- [ ] **Step 7: Commit**

```bash
git add platformio.ini src/main.cpp include/secrets.h.example README.md
git commit -m "feat: OTA via rede local, documentacao atualizada"
```

---

## Cobertura da spec

| Requisito de `specs/telemetria.md` | Task |
|---|---|
| Buffer circular em RAM, overflow descarta o mais antigo | 1 |
| Decisão de retry por código HTTP (2xx/4xx/5xx/conexão) | 2 |
| Payload JSON no formato exato do backend, `ArduinoJson` | 3 |
| `horaAtualUnix()`/`relogioSincronizado()` | 4 |
| Watchdog, alimentado por volta do loop e por lote | 5 |
| Ciclo de esvaziamento a cada 60s, até `MAX_LOTES_POR_CICLO` | 6 |
| Não grava amostra antes do relógio sincronizar | 6 |
| Integração em `main.cpp` sem regressão do Wi-Fi/medição | 6 |
| OTA via `ArduinoOTA` | 7 |
| Documentação (`README.md`, `secrets.h.example`) | 6, 7 |

## Fora de escopo deste plano

Persistência do buffer em NVS/flash, contador de sequência no payload, HTTPS/TLS, extensão do schema de `health` no backend, Fase 6 (PC817, instalação física) — todos já listados como fora de escopo em `specs/telemetria.md` §10.
