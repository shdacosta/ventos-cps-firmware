# Contagem de Reconexões de Wi-Fi — Plano de Implementação

> **Para executores agênticos:** SUB-SKILL OBRIGATÓRIA: usar `superpowers:subagent-driven-development` (recomendado) ou `superpowers:executing-plans` para implementar tarefa a tarefa. Os passos usam checkbox (`- [ ]`) para rastreio.

**Goal:** Contar reconexões de Wi-Fi de verdade (não a conexão inicial do boot) no firmware, e fazer esse número chegar ao backend e à SPA — o dispositivo fica no alto de uma caixa d'água, o dado só tem valor de diagnóstico remoto se sair de lá.

**Architecture:** Cross-repo. `wifi_gerenciado.cpp` (firmware) é a única fonte de verdade do contador — detecta a transição "caiu → reconectou" via uma flag de estado nova. Daí em diante é transporte mecânico: `telemetria.cpp` monta o campo no JSON, `envio.cpp` passa o valor, o backend só armazena e expõe (mesmo padrão já usado por `uptime_seconds`/`free_heap_bytes`/`wifi_rssi_dbm` — nenhuma lógica de negócio sobre o valor do lado do servidor), a SPA só exibe.

**Tech Stack:** C++/PlatformIO (firmware), Python/FastAPI/Pydantic/SQLite (backend), Vue 3 (SPA).

**Spec:** [`specs/reconexoes-wifi.md`](specs/reconexoes-wifi.md) (neste repositório, `ventos-cps-firmware`) · Pré-requisito: `specs/telemetria.md §10` (já tinha adiado esta extensão de schema deliberadamente)

## Global Constraints

- O contador só incrementa numa reconexão de verdade (caiu depois de já ter conectado) — a conexão inicial do boot NUNCA incrementa
- Uma queda longa com várias tentativas malsucedidas conta como **uma** reconexão quando finalmente reconecta, não uma por tentativa
- Contador reseta a cada boot (mesmo padrão de `uptime_seconds`) — sem persistência em NVS
- Nome do campo, ponta a ponta: `wifi_reconnect_count` (JSON, coluna SQL, schema Pydantic, prop Vue) — sem tradução de nome entre camadas
- Campo opcional em todo lugar do backend (`int | None`, coluna nullable) — firmware antigo sem esse campo continua sendo aceito
- Correção adjacente autorizada: `backoffAtualMs` (reconexão Wi-Fi) passa a resetar pro valor inicial também numa reconexão de verdade, não só na conexão inicial do boot
- Commits em português, conventional commits, SEM menção a IA e SEM Co-Authored-By

---

## Estrutura de arquivos

| Arquivo | Repositório | Responsabilidade |
|---|---|---|
| `include/wifi_gerenciado.h`, `src/wifi_gerenciado.cpp` | `ventos-cps-firmware` | Detecção da transição, contador, `wifiContagemReconexoes()`, correção do backoff |
| `include/telemetria.h`, `src/telemetria.cpp` | `ventos-cps-firmware` | Campo novo no payload JSON — testável nativamente |
| `src/envio.cpp` | `ventos-cps-firmware` | Passa `wifiContagemReconexoes()` pro payload |
| `backend/migrations/002_wifi_reconnect_count.sql` | `ventos-campinas` | Coluna nova em `device_health` |
| `backend/app/schemas.py` | `ventos-campinas` | `HealthPayload`/`DeviceHealth` ganham o campo |
| `backend/app/repository.py` | `ventos-campinas` | `salvar_saude()` grava a coluna nova |
| `backend/README.md` | `ventos-campinas` | Dicionário de dados + contrato do payload |
| `web/src/components/SaudeDispositivo.vue` | `ventos-campinas` | Exibe o campo na SPA |

---

## Task 1: `wifi_gerenciado` — detecção da transição e contador

**Repositório:** `/Users/sergio/projects/ventos-cps-firmware`

**Files:**
- Modify: `include/wifi_gerenciado.h`
- Modify: `src/wifi_gerenciado.cpp`

**Interfaces:**
- Consumes: nada de tasks anteriores
- Produces: `uint32_t wifiContagemReconexoes()`

⚠️ **Sem teste automatizado** — toca `WiFi.h` real, só compila para `esp32dev`. Verificação por compilação + revisão de código cuidadosa (é o único ponto de lógica real de todo o ciclo) + teste ao vivo opcional se o dispositivo estiver disponível.

- [ ] **Step 1: Ler os dois arquivos atuais antes de editar**

Use a ferramenta de leitura para ver o conteúdo exato — `wifi_gerenciado.cpp` já tem `iniciarWifi()`, `atualizarWifi()`, `wifiConectado()`, `wifiRssiDbm()`, `wifiIp()`, `horaAtualFormatada()`, `relogioSincronizado()`, `horaAtualUnix()` funcionando desde as Fases 4/5. Você vai substituir só o corpo de `atualizarWifi()` e acrescentar estado/função nova — o resto do arquivo não muda.

- [ ] **Step 2: Acrescentar estado novo em `src/wifi_gerenciado.cpp`**

No namespace anônimo, logo depois de `bool ntpFoiPedido = false;`:

```cpp
uint32_t contadorReconexoes      = 0;
bool     caiuDesdeAUltimaConexao = false;
```

- [ ] **Step 3: Substituir o corpo de `atualizarWifi()`**

Troque a função inteira por esta versão (a única mudança real é o `else if` novo dentro do bloco conectado, a linha `caiuDesdeAUltimaConexao = true;` no bloco desconectado, e os comentários atualizados — o resto, incluindo o backoff exponencial, é idêntico):

```cpp
void atualizarWifi()
{
    if (WiFi.status() == WL_CONNECTED) {
        if (!jaConectouAlgumaVez) {
            jaConectouAlgumaVez = true;
            backoffAtualMs = BACKOFF_INICIAL_MS;
            Serial.printf("[wifi] conectado. ip=%s rssi=%ddBm\n",
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());
        } else if (caiuDesdeAUltimaConexao) {
            // Reconexao de verdade (nao a conexao inicial do boot) --
            // unico lugar onde o contador incrementa. Reseta o backoff
            // tambem aqui -- antes disso so era resetado na conexao
            // inicial, entao depois da primeira queda o backoff nunca
            // voltava a descer do teto de 30s, mesmo com a rede
            // estavel por horas.
            contadorReconexoes++;
            caiuDesdeAUltimaConexao = false;
            backoffAtualMs = BACKOFF_INICIAL_MS;
            Serial.printf("[wifi] reconectado (total=%lu). ip=%s rssi=%ddBm\n",
                          (unsigned long) contadorReconexoes,
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());
        }
        if (!ntpFoiPedido) {
            pedirSincronizacaoNtp();
        }
        return;
    }

    // Perdeu a conexao depois de ja ter conectado uma vez -- reseta o
    // estado do NTP e marca que uma reconexao de verdade (nao o boot)
    // esta pendente, para o contador incrementar quando reconectar.
    if (jaConectouAlgumaVez) {
        ntpFoiPedido = false;
        caiuDesdeAUltimaConexao = true;
    }

    const uint32_t agora = millis();
    if (agora < proximaTentativaEm) {
        return;  // ainda dentro da janela de espera do backoff
    }

    Serial.printf("[wifi] desconectado, tentando reconectar (proxima espera: %lus)\n",
                  (unsigned long) (backoffAtualMs / 1000));
    WiFi.disconnect();
    WiFi.reconnect();

    proximaTentativaEm = agora + backoffAtualMs;

    backoffAtualMs *= 2;
    if (backoffAtualMs > BACKOFF_MAXIMO_MS) {
        backoffAtualMs = BACKOFF_MAXIMO_MS;
    }
}
```

- [ ] **Step 4: Acrescentar a função pública, no final de `src/wifi_gerenciado.cpp`**

```cpp
uint32_t wifiContagemReconexoes()
{
    return contadorReconexoes;
}
```

- [ ] **Step 5: Acrescentar a declaração em `include/wifi_gerenciado.h`**

Junto das outras declarações (`wifiConectado`, `wifiRssiDbm`, etc.):

```cpp
// Reconexoes de verdade desde o boot -- NAO conta a conexao inicial.
// Reseta a cada reinicio (mesmo padrao de uptime_seconds, que tambem
// zera no boot -- os dois sao "desde quando o dispositivo esta de pe").
uint32_t wifiContagemReconexoes();
```

- [ ] **Step 6: Compilar para o ESP32 real**

Run: `pio run -e esp32dev`
Expected: `SUCCESS`

- [ ] **Step 7: Confirmar que os testes nativos continuam passando**

Run: `pio test -e native`
Expected: `0 Failures` — `wifi_gerenciado.cpp` não é compilado em `env:native`, então isso só confirma que nada quebrou por engano

- [ ] **Step 8 (opcional, se o dispositivo estiver disponível): teste ao vivo**

Grave o firmware, deixe conectar normalmente, depois derrube o Wi-Fi de propósito por alguns segundos (ex.: desligar o roteador ou trocar a senha temporariamente) e observe o monitor serial: deve aparecer `[wifi] desconectado...` seguido, quando a rede voltar, de `[wifi] reconectado (total=1)...`. Confirme que o boot inicial NÃO tinha aparecido como "reconectado" (só "conectado").

- [ ] **Step 9: Commit**

```bash
git add include/wifi_gerenciado.h src/wifi_gerenciado.cpp
git commit -m "feat: conta reconexoes de wifi de verdade, corrige reset do backoff"
```

---

## Task 2: Payload — `telemetria.{h,cpp}` + `envio.cpp`

**Repositório:** `/Users/sergio/projects/ventos-cps-firmware`

**Files:**
- Modify: `include/telemetria.h`
- Modify: `src/telemetria.cpp`
- Modify: `test/test_telemetria/test_telemetria.cpp`
- Modify: `src/envio.cpp`

**Interfaces:**
- Consumes: `wifiContagemReconexoes()` (Task 1)
- Produces: `montarPayloadJson` ganha o parâmetro `wifiReconnectCount`

- [ ] **Step 1: Atualizar o teste de estrutura em `test/test_telemetria/test_telemetria.cpp`**

Ache `test_montar_payload_estrutura_correta` e troque a chamada e as asserções:

```cpp
void test_montar_payload_estrutura_correta(void) {
    AmostraTelemetria amostras[2] = {
        {1755432000, 3.21f, 5.84f},
        {1755432010, 3.44f, 4.10f},
    };

    char saida[512];
    size_t escrito = montarPayloadJson(amostras, 2, "anemometro-01", "1.0.0",
                                        86400, 351476, -62, 3, saida, sizeof(saida));

    TEST_ASSERT_TRUE(escrito > 0);

    JsonDocument doc;
    DeserializationError erro = deserializeJson(doc, saida, escrito);
    TEST_ASSERT_TRUE(erro == DeserializationError::Ok);

    TEST_ASSERT_EQUAL_STRING("anemometro-01", doc["device_id"]);
    TEST_ASSERT_EQUAL_STRING("1.0.0", doc["firmware_version"]);
    TEST_ASSERT_EQUAL_UINT32(86400, doc["health"]["uptime_seconds"]);
    TEST_ASSERT_EQUAL_UINT32(351476, doc["health"]["free_heap_bytes"]);
    TEST_ASSERT_EQUAL_INT(-62, doc["health"]["wifi_rssi_dbm"]);
    TEST_ASSERT_EQUAL_UINT32(3, doc["health"]["wifi_reconnect_count"]);

    TEST_ASSERT_EQUAL_UINT32(2, doc["samples"].size());
    TEST_ASSERT_EQUAL_UINT32(1755432000, doc["samples"][0]["measured_at"]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.21f, doc["samples"][0]["avg_speed_ms"]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.84f, doc["samples"][0]["gust_speed_ms"]);
    TEST_ASSERT_EQUAL_UINT32(1755432010, doc["samples"][1]["measured_at"]);
}
```

- [ ] **Step 2: Atualizar as outras duas chamadas de `montarPayloadJson` no mesmo arquivo**

Em `test_montar_payload_buffer_pequeno_demais_devolve_zero`, acrescente `0` como novo argumento (não importa o valor pra este teste):

```cpp
    size_t escrito = montarPayloadJson(amostras, 1, "anemometro-01", "1.0.0",
                                        100, 200000, -50, 0, saidaMinuscula, sizeof(saidaMinuscula));
```

Em `test_montar_payload_500_amostras_pior_caso_cabe_no_buffer`, use o maior valor possível de `uint32_t` (mantém o espírito de "pior caso de tamanho" do teste, incluindo o novo campo):

```cpp
    size_t escrito = montarPayloadJson(amostras, MAX_AMOSTRAS_POR_LOTE,
                                        "anemometro-01", "1.0.0",
                                        999999999, 999999999, -100, 4294967295u,
                                        saida, sizeof(saida));
```

- [ ] **Step 3: Rodar e confirmar que falha**

Run: `pio test -e native`
Expected: FAIL — erro de compilação, número de argumentos não bate com a assinatura atual de `montarPayloadJson`

- [ ] **Step 4: Atualizar a declaração em `include/telemetria.h`**

Ache a declaração de `montarPayloadJson` e troque por:

```cpp
size_t montarPayloadJson(const AmostraTelemetria* amostras, uint32_t total,
                          const char* deviceId, const char* firmwareVersion,
                          uint32_t uptimeSeconds, uint32_t freeHeapBytes,
                          int wifiRssiDbm, uint32_t wifiReconnectCount,
                          char* saida, size_t capacidadeSaida);
```

- [ ] **Step 5: Atualizar a implementação em `src/telemetria.cpp`**

Ache a definição de `montarPayloadJson` — troque a linha de assinatura e acrescente uma linha no bloco `health`:

```cpp
size_t montarPayloadJson(const AmostraTelemetria* amostras, uint32_t total,
                          const char* deviceId, const char* firmwareVersion,
                          uint32_t uptimeSeconds, uint32_t freeHeapBytes,
                          int wifiRssiDbm, uint32_t wifiReconnectCount,
                          char* saida, size_t capacidadeSaida)
{
    JsonDocument doc;

    doc["device_id"] = deviceId;
    doc["firmware_version"] = firmwareVersion;

    JsonObject health = doc["health"].to<JsonObject>();
    health["uptime_seconds"] = uptimeSeconds;
    health["free_heap_bytes"] = freeHeapBytes;
    health["wifi_rssi_dbm"] = wifiRssiDbm;
    health["wifi_reconnect_count"] = wifiReconnectCount;

    JsonArray samples = doc["samples"].to<JsonArray>();
    for (uint32_t i = 0; i < total; i++) {
        JsonObject amostra = samples.add<JsonObject>();
        amostra["measured_at"] = amostras[i].measuredAt;
        amostra["avg_speed_ms"] = amostras[i].avgSpeedMs;
        amostra["gust_speed_ms"] = amostras[i].gustSpeedMs;
    }

    if (doc.overflowed()) {
        return 0;
    }

    const size_t escrito = serializeJson(doc, saida, capacidadeSaida);
    if (escrito == 0 || escrito >= capacidadeSaida) {
        return 0;
    }
    return escrito;
}
```

(mantenha o resto do arquivo — outras funções — intocado; só a assinatura e o corpo desta função mudam)

- [ ] **Step 6: Rodar e confirmar que passa**

Run: `pio test -e native`
Expected: `0 Failures`

- [ ] **Step 7: Atualizar `src/envio.cpp`**

Ache a chamada de `montarPayloadJson` dentro de `tentarEnviarLotes()` e acrescente `wifiContagemReconexoes()` na mesma posição de `wifiRssiDbm()`:

```cpp
        const size_t escrito = montarPayloadJson(
            loteSaida, n, DEVICE_ID, FIRMWARE_VERSION,
            (uint32_t) (millis() / 1000), ESP.getFreeHeap(), wifiRssiDbm(),
            wifiContagemReconexoes(),
            payloadJson, CAPACIDADE_PAYLOAD_JSON);
```

(`envio.cpp` já inclui `wifi_gerenciado.h`, não precisa de include novo)

- [ ] **Step 8: Compilar os três ambientes de hardware**

Run: `pio run -e esp32dev` — Expected: `SUCCESS`
Run: `pio run -e calibracao` — Expected: `SUCCESS`
Run: `pio run -e ota` — Expected: `SUCCESS`

- [ ] **Step 9: Commit**

```bash
git add include/telemetria.h src/telemetria.cpp src/envio.cpp test/test_telemetria/test_telemetria.cpp
git commit -m "feat: inclui contagem de reconexoes de wifi no payload de telemetria"
```

---

## Task 3: Backend — migration, schema, repository, README

**Repositório:** `/Users/sergio/projects/ventos-campinas`

**Files:**
- Create: `backend/migrations/002_wifi_reconnect_count.sql`
- Modify: `backend/app/schemas.py`
- Modify: `backend/app/repository.py`
- Modify: `backend/README.md`
- Modify: `backend/tests/test_db.py`
- Modify: `backend/tests/test_schemas.py`
- Modify: `backend/tests/test_repository_escrita.py`
- Modify: `backend/tests/test_service_ingestao.py`
- Modify: `backend/tests/test_api_wind.py`

**Interfaces:**
- Consumes: nada de tasks anteriores (repositório separado)
- Produces: coluna `wifi_reconnect_count` em `device_health`, campo `wifi_reconnect_count` em `HealthPayload`/`DeviceHealth`

- [ ] **Step 1: Escrever os testes que falham**

Em `backend/tests/test_db.py`, acrescente (depois de `test_migrate_e_idempotente`):

```python
def test_migrate_adiciona_coluna_wifi_reconnect_count(tmp_path):
    conn = connect(str(tmp_path / "t.db"))
    migrate(conn)

    colunas = {
        r["name"]
        for r in conn.execute("PRAGMA table_info(device_health)")
    }

    assert "wifi_reconnect_count" in colunas
```

Em `backend/tests/test_schemas.py`, troque `test_aceita_payload_completo` (acrescenta o campo no dict de entrada e uma asserção nova):

```python
def test_aceita_payload_completo():
    payload = IngestPayload.model_validate(
        {
            "device_id": "anemometro-01",
            "firmware_version": "1.0.0",
            "health": {
                "uptime_seconds": 86400,
                "free_heap_bytes": 351476,
                "wifi_rssi_dbm": -62,
                "wifi_reconnect_count": 3,
            },
            "samples": [
                {"measured_at": 1767225600, "avg_speed_ms": 3.21,
                 "gust_speed_ms": 5.84}
            ],
        }
    )

    assert payload.device_id == "anemometro-01"
    assert len(payload.samples) == 1
    assert payload.health.wifi_rssi_dbm == -62
    assert payload.health.wifi_reconnect_count == 3
```

Em `backend/tests/test_repository_escrita.py`, troque `test_salvar_saude_faz_upsert`:

```python
def test_salvar_saude_faz_upsert(conn):
    saude = HealthPayload(
        uptime_seconds=100, free_heap_bytes=200, wifi_rssi_dbm=-60,
        wifi_reconnect_count=1,
    )
    salvar_saude(conn, "anemometro-01", saude, "1.0.0", 1767225600)

    saude2 = HealthPayload(
        uptime_seconds=700, free_heap_bytes=180, wifi_rssi_dbm=-71,
        wifi_reconnect_count=4,
    )
    salvar_saude(conn, "anemometro-01", saude2, "1.1.0", 1767226200)

    linhas = conn.execute("SELECT * FROM device_health").fetchall()
    assert len(linhas) == 1
    assert linhas[0]["uptime_seconds"] == 700
    assert linhas[0]["firmware_version"] == "1.1.0"
    assert linhas[0]["wifi_reconnect_count"] == 4
```

Em `backend/tests/test_service_ingestao.py`, troque `test_ingestao_grava_saude_quando_presente`:

```python
def test_ingestao_grava_saude_quando_presente(conn):
    payload = IngestPayload(
        device_id="anemometro-01",
        firmware_version="1.0.0",
        health=HealthPayload(
            uptime_seconds=99, free_heap_bytes=1000, wifi_rssi_dbm=-55,
            wifi_reconnect_count=2,
        ),
        samples=[_amostra()],
    )

    ingerir(conn, payload, AGORA)

    linha = conn.execute("SELECT * FROM device_health").fetchone()
    assert linha["uptime_seconds"] == 99
    assert linha["firmware_version"] == "1.0.0"
    assert linha["wifi_reconnect_count"] == 2
```

Em `backend/tests/test_api_wind.py`, troque `test_health_traz_estado_do_dispositivo`:

```python
def test_health_traz_estado_do_dispositivo(client):
    payload = {
        "device_id": "anemometro-01",
        "firmware_version": "1.2.3",
        "health": {
            "uptime_seconds": 7200,
            "free_heap_bytes": 340000,
            "wifi_rssi_dbm": -58,
            "wifi_reconnect_count": 5,
        },
        "samples": [
            {"measured_at": AGORA - 10, "avg_speed_ms": 3.0, "gust_speed_ms": 4.0}
        ],
    }
    client.post(
        "/api/v1/ingest", json=payload, headers={"Authorization": f"Bearer {TOKEN}"}
    )

    corpo = client.get("/api/v1/device/health").json()

    assert corpo["uptime_seconds"] == 7200
    assert corpo["firmware_version"] == "1.2.3"
    assert corpo["wifi_rssi_dbm"] == -58
    assert corpo["wifi_reconnect_count"] == 5
```

- [ ] **Step 2: Rodar e confirmar que falha**

Run: `make api-test PYTEST_ARGS="-v"` (da raiz do repositório — o alvo já cuida do venv certo, ver `Makefile`)
Expected: FAIL — `wifi_reconnect_count` não existe em `HealthPayload`/`DeviceHealth` (erro de validação Pydantic ou `AttributeError`), e a coluna não existe na tabela

- [ ] **Step 3: Criar a migration**

`backend/migrations/002_wifi_reconnect_count.sql`:

```sql
ALTER TABLE device_health ADD COLUMN wifi_reconnect_count INTEGER;
```

- [ ] **Step 4: Atualizar `backend/app/schemas.py`**

Troque as duas classes:

```python
class HealthPayload(BaseModel):
    uptime_seconds: int | None = None
    free_heap_bytes: int | None = None
    wifi_rssi_dbm: int | None = None
    wifi_reconnect_count: int | None = None
```

```python
class DeviceHealth(BaseModel):
    device_id: str
    updated_at: int
    uptime_seconds: int | None
    free_heap_bytes: int | None
    wifi_rssi_dbm: int | None
    wifi_reconnect_count: int | None
    firmware_version: str | None
```

- [ ] **Step 5: Atualizar `backend/app/repository.py`**

Ache `salvar_saude()` e troque por:

```python
def salvar_saude(
    conn: sqlite3.Connection,
    device_id: str,
    health: HealthPayload,
    firmware_version: str | None,
    updated_at: int,
) -> None:
    with conn:
        conn.execute(
            """
            INSERT INTO device_health (device_id, updated_at, uptime_seconds,
                                       free_heap_bytes, wifi_rssi_dbm,
                                       wifi_reconnect_count, firmware_version)
            VALUES (?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(device_id) DO UPDATE SET
                updated_at           = excluded.updated_at,
                uptime_seconds       = excluded.uptime_seconds,
                free_heap_bytes      = excluded.free_heap_bytes,
                wifi_rssi_dbm        = excluded.wifi_rssi_dbm,
                wifi_reconnect_count = excluded.wifi_reconnect_count,
                firmware_version     = excluded.firmware_version
            """,
            (device_id, updated_at, health.uptime_seconds,
             health.free_heap_bytes, health.wifi_rssi_dbm,
             health.wifi_reconnect_count, firmware_version),
        )
```

(`service.py` não precisa de mudança — `ingerir()` já repassa `payload.health` inteiro pra `salvar_saude()`, sem tocar campo por campo)

- [ ] **Step 6: Rodar e confirmar que passa**

Run: `make api-test PYTEST_ARGS="-v"`
Expected: `0 failed` — deve reportar 99 testes (98 atuais + `test_migrate_adiciona_coluna_wifi_reconnect_count`, o único teste genuinamente novo; os outros 4 testes do Step 1 são modificações de testes já existentes, não somam ao total)

- [ ] **Step 7: Atualizar `backend/README.md`**

Na seção "Tabela `device_health`" (por volta da linha 84), no bloco SQL,
acrescente a coluna **no final**, depois de `firmware_version` — é onde ela
fisicamente fica na tabela de verdade, já que `ALTER TABLE ADD COLUMN` do
SQLite sempre acrescenta ao final (não dá pra escolher a posição); mostrar
outra ordem aqui divergiria do schema real:

```sql
CREATE TABLE device_health (
    device_id        TEXT PRIMARY KEY,
    updated_at       INTEGER NOT NULL,
    uptime_seconds   INTEGER,
    free_heap_bytes  INTEGER,
    wifi_rssi_dbm    INTEGER,
    firmware_version TEXT,
    wifi_reconnect_count INTEGER
);
```

Na tabela de dicionário de dados logo abaixo, acrescente uma linha depois de `wifi_rssi_dbm`:

```markdown
| `wifi_reconnect_count` | INTEGER | — | Quantas vezes o Wi-Fi caiu e reconectou desde o último boot. Sinal de instabilidade de sinal — não confundir com uma queda de `uptime_seconds`: uptime baixo indica reboot, contador de reconexão alto (com uptime normal) indica rede instável sem o dispositivo ter reiniciado. |
```

Logo abaixo da tabela, troque "Estas três métricas são o sistema de alerta precoce..." por "Estas quatro métricas são o sistema de alerta precoce...".

Na seção "Payload de ingestão", no exemplo JSON do bloco `health`, acrescente a linha:

```json
  "health": {
    "uptime_seconds": 86400,
    "free_heap_bytes": 351476,
    "wifi_rssi_dbm": -62,
    "wifi_reconnect_count": 3
  },
```

- [ ] **Step 8: Commit**

```bash
git add backend/migrations/002_wifi_reconnect_count.sql backend/app/schemas.py backend/app/repository.py backend/README.md backend/tests/test_db.py backend/tests/test_schemas.py backend/tests/test_repository_escrita.py backend/tests/test_service_ingestao.py backend/tests/test_api_wind.py
git commit -m "feat: aceita e persiste contagem de reconexoes de wifi no health do dispositivo"
```

---

## Task 4: SPA — exibir a contagem de reconexões

**Repositório:** `/Users/sergio/projects/ventos-campinas`

**Files:**
- Modify: `web/src/components/SaudeDispositivo.vue`

**Interfaces:**
- Consumes: `wifi_reconnect_count` do backend (Task 3) — chega pronto na prop `saude`, sem transformação

- [ ] **Step 1: Ler o arquivo atual**

Use a ferramenta de leitura pra ver o conteúdo exato — o componente é pequeno (31 linhas), você vai só acrescentar um `<span>` novo no template, sem mexer no `<script setup>`.

- [ ] **Step 2: Acrescentar o campo no template**

No `<footer>`, logo depois do `<span>` que mostra `saude.wifi_rssi_dbm`:

```vue
    <span>{{ saude.wifi_rssi_dbm }} dBm</span>
    <span v-if="saude.wifi_reconnect_count !== null">{{ saude.wifi_reconnect_count }} reconexões</span>
    <span>{{ saude.firmware_version }}</span>
```

(o `v-if` evita mostrar "null reconexões" em dado histórico de antes desta migration — os outros campos não têm essa guarda porque não existiam antes de existir o backend, mas este campo especificamente pode ser `null` em linhas antigas)

- [ ] **Step 3: Confirmar visualmente**

Suba a SPA (`make web-dev` — Vite com HMR) e confirme que o rodapé de saúde do dispositivo renderiza sem erro no console, com ou sem o backend rodando (o componente já trata `saude === null` no `v-if` do `<footer>` inteiro).

- [ ] **Step 4: Commit**

```bash
git add web/src/components/SaudeDispositivo.vue
git commit -m "feat: exibe contagem de reconexoes de wifi na saude do dispositivo"
```

---

## Cobertura da spec

| Requisito de `specs/reconexoes-wifi.md` | Task |
|---|---|
| Detecção da transição, contador, `wifiContagemReconexoes()` | 1 |
| Correção adjacente do reset de backoff (§3.1) | 1 |
| Payload — `telemetria.h/cpp`, `envio.cpp` (§4.1, §4.2) | 2 |
| Migration, `schemas.py`, `repository.py` (§4.3-4.5) | 3 |
| `backend/README.md` (§4.6) | 3 |
| SPA (§5) | 4 |
| Testes nativos do payload, testes backend (§6) | 2, 3 |

## Fora de escopo deste plano

Persistência do contador em NVS, alertas automáticos baseados no valor, qualquer outro campo de saúde além deste — todos já listados como fora de escopo em `specs/reconexoes-wifi.md §8`.
