# Spec — Contagem de reconexões de Wi-Fi

**Design aprovado em 2026-08-20.**

Pré-requisito já decidido: [firmware.md §5](firmware.md#5-manutenibilidade)
("Métricas de saúde no payload: uptime, heap livre, RSSI, contagem de
reconexões" — os três primeiros já implementados na Fase 4/5, este é o
quarto que faltava). [telemetria.md §10](telemetria.md) já tinha adiado
isso deliberadamente ("extensão do schema de health... fora do escopo de
um ciclo só de firmware") — este ciclo resolve essa pendência cross-repo.

---

## 1. Objetivo

Contar quantas vezes o Wi-Fi reconectou (depois de cair, não a conexão
inicial do boot) e fazer esse número chegar ao backend/SPA — o dispositivo
fica no alto de uma caixa d'água, ninguém vai plugar um monitor serial lá
pra ver isso. Sem chegar ao backend, o contador não cumpre o propósito de
diagnóstico remoto que motivou o requisito original.

---

## 2. Decisão estruturante: onde a lógica vive

| Camada | Responsabilidade |
|---|---|
| `wifi_gerenciado.cpp` (firmware) | Detecta a transição "caiu → reconectou", incrementa o contador. Única fonte de verdade do valor. |
| `telemetria.cpp`/`envio.cpp` (firmware) | Só transporta o valor já calculado até o payload — nenhuma lógica de decisão aqui. |
| Backend | Só armazena e expõe — nenhuma lógica de negócio sobre o valor (mesmo padrão de `uptime_seconds`/`free_heap_bytes`/`wifi_rssi_dbm`, que já passam direto de `HealthPayload` até `device_health` sem transformação). |

O ponto mais delicado do ciclo inteiro é a detecção da transição em
`wifi_gerenciado.cpp` — é lógica de estado, não cálculo, e é o único lugar
onde um erro poderia inflar ou nunca incrementar o contador. Tudo depois
disso (payload, schema, coluna, template Vue) é transporte mecânico do
mesmo valor, sem risco de lógica.

---

## 3. `wifi_gerenciado.{h,cpp}` — detecção da transição

### Estado atual (antes desta spec)

```cpp
uint32_t proximaTentativaEm = 0;
uint32_t backoffAtualMs     = BACKOFF_INICIAL_MS;
bool     jaConectouAlgumaVez = false;
bool     ntpFoiPedido        = false;
```

`atualizarWifi()` já tem um bloco que detecta "caiu depois de já ter
conectado uma vez" (`if (jaConectouAlgumaVez) { ntpFoiPedido = false; }`,
dentro do ramo de desconectado) — mas hoje ele só mexe no estado do NTP,
nunca marca "isto foi uma queda real". É esse blocos que precisa ganhar
uma flag nova.

### Estado novo

```cpp
uint32_t contadorReconexoes      = 0;
bool     caiuDesdeAUltimaConexao = false;
```

### Lógica (substituindo o corpo de `atualizarWifi()`)

```
se WiFi.status() == WL_CONNECTED:
    se NUNCA conectou antes (!jaConectouAlgumaVez):
        marca jaConectouAlgumaVez = true
        reseta backoffAtualMs pro valor inicial
        loga "[wifi] conectado..."
    senao se caiu desde a ultima conexao (caiuDesdeAUltimaConexao):
        // reconexao de verdade, nao o boot inicial
        contadorReconexoes++
        caiuDesdeAUltimaConexao = false
        reseta backoffAtualMs pro valor inicial   // ver §3.1, correcao adjacente
        loga "[wifi] reconectado (total=N)..."
    se NTP ainda nao foi pedido: pede
    retorna

// dai pra baixo, desconectado:
se ja conectou alguma vez:
    reseta estado do NTP
    marca caiuDesdeAUltimaConexao = true   // ⚠️ NOVO -- so aqui essa flag liga

... resto do backoff exponencial, sem mudanca ...
```

`caiuDesdeAUltimaConexao` só liga dentro do ramo "já conectou alguma vez
E não está conectado agora" — ou seja, nunca liga durante o boot inicial
(quando `jaConectouAlgumaVez` ainda é `false`), então o contador nunca
incrementa na primeira conexão. Só liga uma vez por queda (a checagem
`WiFi.status()` já é feita a cada `loop()`, mas a flag não é
re-setada enquanto continua desconectado — só *desliga* de novo quando a
reconexão de fato acontece), então uma queda longa com múltiplas
tentativas de reconexão malsucedidas conta como **uma** reconexão quando
finalmente reconecta, não uma por tentativa.

### 3.1 Correção adjacente: reset do backoff também na reconexão

**Achado durante esta spec, não pedido originalmente:** `backoffAtualMs`
hoje só é resetado pro valor inicial (`BACKOFF_INICIAL_MS`) dentro do
`if (!jaConectouAlgumaVez)` — ou seja, **nunca** numa reconexão de
verdade. Isso significa que depois da primeira queda, o backoff sobe até
o teto (30s) e nunca mais volta a descer, mesmo que a rede volte a ficar
estável por horas — a próxima queda já começa esperando o máximo, não o
mínimo. O comentário original ("reseta para a próxima queda") sugere que
esse já era o comportamento pretendido, só não foi implementado no ramo
certo. Corrigido junto nesta spec, no mesmo bloco `else if` que detecta a
reconexão (linha "reseta backoffAtualMs pro valor inicial" acima).

### Interface pública nova

```cpp
// Reconexoes de verdade desde o boot -- NAO conta a conexao inicial.
// Reseta a cada reinicio (mesmo padrao de uptime_seconds, que tambem
// zera no boot -- os dois sao "desde quando o dispositivo esta de pe").
uint32_t wifiContagemReconexoes();
```

---

## 4. Payload e contrato do backend

### 4.1 `telemetria.h`/`telemetria.cpp` (firmware)

```cpp
size_t montarPayloadJson(const AmostraTelemetria* amostras, uint32_t total,
                          const char* deviceId, const char* firmwareVersion,
                          uint32_t uptimeSeconds, uint32_t freeHeapBytes,
                          int wifiRssiDbm, uint32_t wifiReconnectCount,
                          char* saida, size_t capacidadeSaida);
```

Um campo novo no objeto `health` do JSON: `wifi_reconnect_count`.

### 4.2 `envio.cpp` (firmware)

A chamada de `montarPayloadJson` ganha `wifiContagemReconexoes()` como
argumento novo, na mesma posição de `wifiRssiDbm()` — vindo de
`wifi_gerenciado.h`, sem lógica adicional.

### 4.3 Backend — `backend/app/schemas.py`

```python
class HealthPayload(BaseModel):
    uptime_seconds: int | None = None
    free_heap_bytes: int | None = None
    wifi_rssi_dbm: int | None = None
    wifi_reconnect_count: int | None = None


class DeviceHealth(BaseModel):
    device_id: str
    updated_at: int
    uptime_seconds: int | None
    free_heap_bytes: int | None
    wifi_rssi_dbm: int | None
    wifi_reconnect_count: int | None
    firmware_version: str | None
```

`int | None` pelo mesmo motivo dos outros três campos de saúde: firmware
mais antigo (sem esse campo) continua aceito, o valor só fica `NULL`.

### 4.4 Backend — migration `backend/migrations/002_wifi_reconnect_count.sql`

```sql
ALTER TABLE device_health ADD COLUMN wifi_reconnect_count INTEGER;
```

Nullable, sem valor padrão — mesmo padrão das colunas de saúde
existentes. Linhas antigas ficam com `NULL` (não zero — "não sabemos",
diferente de "sabemos que foi zero").

### 4.5 Backend — `backend/app/repository.py`

`salvar_saude()` ganha a coluna nova no `INSERT`/`ON CONFLICT DO UPDATE`
e na tupla de parâmetros — mecânico, mesmo padrão das colunas existentes.
`saude()` já faz `SELECT *`, não precisa de mudança.

`service.py` não muda — `ingerir()` repassa `payload.health` inteiro pra
`salvar_saude()`, sem tocar campo por campo.

### 4.6 `backend/README.md`

- Tabela `device_health`: linha nova (`wifi_reconnect_count`, unidade
  "—", explicação: "quantas vezes o Wi-Fi caiu e reconectou desde o
  último boot — sinal de instabilidade de sinal; queda pra zero junto
  com uptime indica reboot, não reconexão").
- Payload de ingestão: campo novo no exemplo JSON do bloco `health`.

---

## 5. SPA — `SaudeDispositivo.vue`

Um `<span>` novo no rodapé, mesmo padrão dos existentes
(`{{ saude.wifi_reconnect_count }}`) — sem transformação, o campo já
chega pronto do backend. Precisa de um guard `v-if="saude.wifi_reconnect_count != null"`
no span inteiro — decisão refinada no plano de implementação, diferente do
que esta spec previa originalmente: sem o guard, dado histórico
pré-migration (`NULL`) ou payload de firmware mais antigo (campo ausente)
renderizaria " reconexões" sem número, já que Vue só esvazia o conteúdo
interpolado, não omite o `<span>` sozinho. A comparação usa `!=` (frouxa),
não `!==`, porque precisa cobrir `null` E `undefined` na mesma checagem.

---

## 6. Testes

### Nativos (`env:native`) — firmware

`wifi_gerenciado.cpp` toca `WiFi.h` real — **sem teste nativo possível**,
mesma categoria de `anemometro.cpp`/`envio.cpp`. Verificação por
compilação + revisão de código + teste ao vivo (se o dispositivo estiver
disponível: derrubar o Wi-Fi de propósito — ex. desligar o roteador
alguns segundos — e confirmar no monitor serial que `contadorReconexoes`
incrementa só na reconexão, não no boot).

`montarPayloadJson` (native, já testável) ganha o campo novo na
verificação de estrutura — o teste existente
`test_montar_payload_estrutura_correta` passa a conferir também
`wifi_reconnect_count`.

### Backend (`pytest`)

- `test_schemas.py`: `HealthPayload`/`DeviceHealth` aceitam
  `wifi_reconnect_count`.
- `test_repository_escrita.py::test_salvar_saude_faz_upsert`: fixture
  ganha o campo, assert confirma que persiste.
- `test_service_ingestao.py::test_ingestao_grava_saude_quando_presente`:
  idem.
- `test_api_wind.py::test_health_traz_estado_do_dispositivo`: confirma
  que `GET /api/v1/device/health` devolve o campo.
- Novo: teste de migration confirmando que `002_wifi_reconnect_count.sql`
  aplica limpo sobre o schema de `001_initial.sql` (mesmo padrão de
  `test_db.py` já existente).

---

## 7. Riscos conhecidos

| Risco | Mitigação |
|---|---|
| Detecção de transição errada infla ou nunca incrementa o contador | É o único ponto de lógica real do ciclo inteiro — revisão de código cuidadosa nesse bloco específico; teste ao vivo derrubando o Wi-Fi de propósito, se o dispositivo estiver disponível na hora da implementação |
| Contador reseta a cada boot, não é cumulativo pra sempre | Decisão deliberada (ver §3, interface pública) — mesmo padrão de `uptime_seconds`; um contador "desde sempre" exigiria persistência em NVS, desproporcional ao valor do dado |
| Firmware antigo (sem este campo) continua mandando payload sem `wifi_reconnect_count` | Campo opcional no Pydantic (mesmo padrão dos outros três campos de saúde) já trata isso — sem quebrar ingestão de dispositivos não atualizados |
| Migration falha em produção se já existir alguma coluna com nome parecido | Baixo risco — schema atual conhecido e revisado (`001_initial.sql` é o único arquivo hoje); `ALTER TABLE ADD COLUMN` é idempotente-seguro dentro do runner de migration existente (transação própria, rollback automático em erro) |

---

## 8. Fora de escopo deste ciclo

- Persistência do contador entre reinicializações (NVS) — decisão
  deliberada, ver §3 e riscos
- Alertas automáticos baseados em `wifi_reconnect_count` alto (ex.: SPA
  destacar visualmente instabilidade) — só exposição do dado neste ciclo
- Qualquer outro campo de saúde além deste (ex.: contagem de pacotes NTP
  falhos) — fora do requisito original de `firmware.md §5`
