# Captura de pulsos e medição — Plano de Implementação

> **Para executores agênticos:** SUB-SKILL OBRIGATÓRIA: usar `superpowers:subagent-driven-development` (recomendado) ou `superpowers:executing-plans` para implementar tarefa a tarefa. Os passos usam checkbox (`- [ ]`) para rastreio.

**Goal:** Transformar pulsos do reed switch em `avg_speed_ms`/`gust_speed_ms` por janela de 10 s, com a matemática testada nativamente (sem hardware) e a captura via ISR pronta para o dupont chegar.

**Architecture:** Dois módulos com fronteira estrita: `medicao.{h,cpp}` é matemática pura (testável no Mac, sem placa), `anemometro.{h,cpp}` é a única parte que toca hardware (ISR, GPIO). `anemometro.cpp` chama a função pura de gravação em buffer de `medicao.cpp` — mesmo o "buffer no limite" fica testável nativamente.

**Tech Stack:** PlatformIO, C++ sobre Arduino core (ESP32), PlatformIO `env:native` + Unity para testes sem hardware.

**Spec:** [`specs/medicao.md`](specs/medicao.md) · Pré-requisitos: [`specs/hardware.md`](specs/hardware.md), [`specs/firmware.md`](specs/firmware.md)

## Global Constraints

- Janela de reporte: **10 s** (contrato já implementado no backend — não é escolha nova)
- Janela da rajada: **3 s** deslizante, dentro da janela de 10 s
- Timeout de calmaria: **10 s** sem pulso → velocidade instantânea = 0
- Debounce: **5 ms**, comparado via `micros()` dentro da própria ISR
- GPIO do sensor: **25**, `INPUT_PULLUP`, interrupção `FALLING` (ligação direta de bancada — `RISING`/PC817 é Fase 6, fora de escopo)
- Fórmula: `v(m/s) = 1319 / (T(ms) * PULSOS_POR_VOLTA)`, com `PULSOS_POR_VOLTA = 1.0f` isolado como `constexpr`
- Média: `freqHz = contagem / 10.0; avgSpeedMs = 1.319 * freqHz / PULSOS_POR_VOLTA`
- Rajada: mesma fórmula de frequência→velocidade, aplicada a cada posição de uma janela deslizante de 3 s
- Buffer de timestamps: **320 posições** (`uint32_t`), pior caso físico é 284 pulsos em 10 s (135 km/h)
- ISR: `IRAM_ATTR`, zero ponto flutuante, zero `Serial`, zero alocação dentro dela
- `lerEZerarJanela()` copia com `portENTER_CRITICAL`/`portEXIT_CRITICAL` — nunca dentro da ISR
- Velocidade instantânea é **só uso local (Serial)** — não vai no payload da Fase 5
- Commits em português, conventional commits, SEM menção a IA e SEM Co-Authored-By
- `make <alvo> ENV=<ambiente>` já é o mecanismo existente do Makefile — não criar alvos novos redundantes

---

## Estrutura de arquivos

| Arquivo | Responsabilidade |
|---|---|
| `platformio.ini` | Ganha `[env:native]` (testes sem hardware) e `[env:calibracao]` (modo de calibração) |
| `include/medicao.h` | Interface: constante de calibração, conversões, `Amostra`, gravação em buffer |
| `src/medicao.cpp` | Implementação pura — zero `#include <Arduino.h>` |
| `test/test_medicao/test_medicao.cpp` | Testes nativos (Unity) de `medicao.cpp` |
| `include/anemometro.h` | Interface: `iniciarAnemometro()`, `JanelaDePulsos`, `lerEZerarJanela()` |
| `src/anemometro.cpp` | ISR, GPIO, buffer — a única parte que toca hardware |
| `src/main.cpp` | Integra `anemometro`+`medicao` ao loop de 10 s; ganha o modo `MODO_CALIBRACAO` |
| `README.md` | Documenta `make test ENV=native` e `make flash ENV=calibracao` |

---

## Task 1: Ambiente de teste nativo

**Files:**
- Modify: `platformio.ini`
- Create: `test/test_medicao/test_medicao.cpp`

**Interfaces:**
- Produces: `[env:native]` funcionando via `pio test -e native`

- [ ] **Step 1: Acrescentar o ambiente nativo ao `platformio.ini`**

No final do arquivo, uma seção nova (não mexe em `[env:esp32dev]`):

```ini
; --- Testes sem hardware -----------------------------------
; Roda no Mac, nao no ESP32. So compila codigo que nao depende
; de Arduino.h/hardware real -- por isso medicao.{h,cpp} e
; deliberadamente livre dessa dependencia.
[env:native]
platform = native
test_framework = unity
```

- [ ] **Step 2: Escrever um teste trivial para provar o toolchain**

`test/test_medicao/test_medicao.cpp`:

```cpp
#include <unity.h>

void test_toolchain_funciona(void) {
    TEST_ASSERT_EQUAL_INT(4, 2 + 2);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_toolchain_funciona);
    return UNITY_END();
}
```

- [ ] **Step 3: Rodar e confirmar que passa**

Run: `pio test -e native`
Expected: `1 Tests 1 Failures 0 Ignored` → `0 Failures` (PASS), saída incluindo `test_toolchain_funciona:PASS`

- [ ] **Step 4: Commit**

```bash
git add platformio.ini test/test_medicao/test_medicao.cpp
git commit -m "feat: ambiente de teste nativo, sem hardware"
```

---

## Task 2: Conversão período→velocidade

**Files:**
- Create: `include/medicao.h`
- Create: `src/medicao.cpp`
- Modify: `test/test_medicao/test_medicao.cpp`

**Interfaces:**
- Produces: `PULSOS_POR_VOLTA` (constexpr float), `periodoParaVelocidadeMs(uint32_t periodoMicros) -> float`

- [ ] **Step 1: Escrever os testes que falham**

Substituir o conteúdo de `test/test_medicao/test_medicao.cpp` (remove o teste trivial da Task 1, ele já cumpriu o papel):

```cpp
#include <unity.h>

#include "medicao.h"

void test_periodo_para_velocidade_partida(void) {
    // 0,7 km/h -> periodo de 6,8s = 6800000 micros (tabela de hardware.md)
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.194f, periodoParaVelocidadeMs(6800000));
}

void test_periodo_para_velocidade_maxima(void) {
    // 135 km/h -> periodo de 35ms = 35000 micros
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 37.5f, periodoParaVelocidadeMs(35000));
}

void test_periodo_para_velocidade_10kmh(void) {
    // 10 km/h -> periodo de 475ms = 475000 micros
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 2.78f, periodoParaVelocidadeMs(475000));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_periodo_para_velocidade_partida);
    RUN_TEST(test_periodo_para_velocidade_maxima);
    RUN_TEST(test_periodo_para_velocidade_10kmh);
    return UNITY_END();
}
```

- [ ] **Step 2: Rodar e confirmar que falha**

Run: `pio test -e native`
Expected: FAIL — `fatal error: 'medicao.h' file not found`

- [ ] **Step 3: Implementar `include/medicao.h`**

```cpp
#pragma once

#include <cstdint>

// Trocar aqui quando o dupont confirmar pulsos/volta
// (specs/pendencias-hardware.md #1). Ver a nota de sinal em
// periodoParaVelocidadeMs sobre por que multiplicar, nao dividir.
constexpr float PULSOS_POR_VOLTA = 1.0f;

// v(m/s) = 1319 / (T(ms) * PULSOS_POR_VOLTA).
//
// Por que multiplicar T por PULSOS_POR_VOLTA, nao dividir o resultado:
// com 2 pulsos/volta, o periodo OBSERVADO entre pulsos e a METADE do
// periodo de uma rotacao completa (2 eventos por volta, nao 1) -- entao
// o periodo real de rotacao e T_observado * PULSOS_POR_VOLTA. Aplicar a
// constante sobre esse periodo corrigido da a velocidade certa direto.
float periodoParaVelocidadeMs(uint32_t periodoMicros);
```

- [ ] **Step 4: Implementar `src/medicao.cpp`**

```cpp
#include "medicao.h"

float periodoParaVelocidadeMs(uint32_t periodoMicros)
{
    if (periodoMicros == 0) {
        return 0.0f;
    }

    const float periodoMs = periodoMicros / 1000.0f;
    return 1319.0f / (periodoMs * PULSOS_POR_VOLTA);
}
```

- [ ] **Step 5: Rodar e confirmar que passa**

Run: `pio test -e native`
Expected: 3 testes, `0 Failures`

- [ ] **Step 6: Commit**

```bash
git add include/medicao.h src/medicao.cpp test/test_medicao/test_medicao.cpp
git commit -m "feat: conversao periodo para velocidade, testada nativamente"
```

---

## Task 3: Gravação em buffer capado — a peça testável do "buffer no limite"

**Files:**
- Modify: `include/medicao.h`
- Modify: `src/medicao.cpp`
- Modify: `test/test_medicao/test_medicao.cpp`

**Interfaces:**
- Consumes: nada de tasks anteriores
- Produces: `gravarTimestampSeCouber(uint32_t* buffer, uint32_t capacidade, uint32_t totalAtual, uint32_t novoTimestamp) -> bool`

- [ ] **Step 1: Escrever os testes que falham**

Acrescentar ao `test/test_medicao/test_medicao.cpp`, antes do `main`:

```cpp
void test_gravar_com_espaco_sobra(void) {
    uint32_t buffer[3] = {0, 0, 0};

    bool gravou = gravarTimestampSeCouber(buffer, 3, 0, 111);

    TEST_ASSERT_TRUE(gravou);
    TEST_ASSERT_EQUAL_UINT32(111, buffer[0]);
}

void test_gravar_na_ultima_posicao_livre(void) {
    uint32_t buffer[3] = {10, 20, 0};

    bool gravou = gravarTimestampSeCouber(buffer, 3, 2, 30);

    TEST_ASSERT_TRUE(gravou);
    TEST_ASSERT_EQUAL_UINT32(30, buffer[2]);
}

void test_gravar_buffer_cheio_nao_escreve_fora(void) {
    uint32_t buffer[3] = {10, 20, 30};

    bool gravou = gravarTimestampSeCouber(buffer, 3, 3, 999);

    TEST_ASSERT_FALSE(gravou);
    // buffer inalterado -- prova que nao escreveu fora dos limites
    TEST_ASSERT_EQUAL_UINT32(10, buffer[0]);
    TEST_ASSERT_EQUAL_UINT32(20, buffer[1]);
    TEST_ASSERT_EQUAL_UINT32(30, buffer[2]);
}
```

E os `RUN_TEST` correspondentes dentro de `main`:

```cpp
    RUN_TEST(test_gravar_com_espaco_sobra);
    RUN_TEST(test_gravar_na_ultima_posicao_livre);
    RUN_TEST(test_gravar_buffer_cheio_nao_escreve_fora);
```

- [ ] **Step 2: Rodar e confirmar que falha**

Run: `pio test -e native`
Expected: FAIL — `gravarTimestampSeCouber` não declarada

- [ ] **Step 3: Acrescentar a `include/medicao.h`**

```cpp
// Grava um timestamp no buffer se houver espaco. Pura: nao mexe em
// estado global, nao aloca -- por isso e testavel nativamente mesmo
// sendo chamada de dentro de uma ISR (anemometro.cpp). Quem chama
// decide o que fazer quando devolve false (normalmente, incrementar
// um contador de descarte).
bool gravarTimestampSeCouber(uint32_t* buffer, uint32_t capacidade,
                              uint32_t totalAtual, uint32_t novoTimestamp);
```

- [ ] **Step 4: Acrescentar a `src/medicao.cpp`**

```cpp
bool gravarTimestampSeCouber(uint32_t* buffer, uint32_t capacidade,
                              uint32_t totalAtual, uint32_t novoTimestamp)
{
    if (totalAtual >= capacidade) {
        return false;
    }

    buffer[totalAtual] = novoTimestamp;
    return true;
}
```

- [ ] **Step 5: Rodar e confirmar que passa**

Run: `pio test -e native`
Expected: 6 testes, `0 Failures`

- [ ] **Step 6: Commit**

```bash
git add include/medicao.h src/medicao.cpp test/test_medicao/test_medicao.cpp
git commit -m "feat: gravacao em buffer capado, testavel sem hardware"
```

---

## Task 4: `Amostra` — velocidade média

**Files:**
- Modify: `include/medicao.h`
- Modify: `src/medicao.cpp`
- Modify: `test/test_medicao/test_medicao.cpp`

**Interfaces:**
- Consumes: `PULSOS_POR_VOLTA` (Task 2)
- Produces: `struct JanelaDePulsos`, `struct Amostra { float avgSpeedMs; float gustSpeedMs; }`, `calcularAmostra(const JanelaDePulsos&) -> Amostra` — **nesta task só a média está correta; a rajada é implementada na Task 5**

- [ ] **Step 1: Escrever os testes que falham**

Acrescentar ao `test/test_medicao/test_medicao.cpp`:

```cpp
void test_amostra_janela_vazia_media_zero(void) {
    JanelaDePulsos janela = {};
    janela.contagem = 0;

    Amostra amostra = calcularAmostra(janela);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, amostra.avgSpeedMs);
}

void test_amostra_media_por_contagem(void) {
    // 21 pulsos em 10s = 2,1 Hz = 10 km/h (tabela de hardware.md)
    JanelaDePulsos janela = {};
    janela.contagem = 21;

    Amostra amostra = calcularAmostra(janela);

    TEST_ASSERT_FLOAT_WITHIN(0.05f, 2.77f, amostra.avgSpeedMs);
}
```

E os `RUN_TEST`:

```cpp
    RUN_TEST(test_amostra_janela_vazia_media_zero);
    RUN_TEST(test_amostra_media_por_contagem);
```

- [ ] **Step 2: Rodar e confirmar que falha**

Run: `pio test -e native`
Expected: FAIL — `JanelaDePulsos`/`Amostra`/`calcularAmostra` não declarados

- [ ] **Step 3: Acrescentar a `include/medicao.h`**

```cpp
struct JanelaDePulsos {
    uint32_t contagem;                 // pulsos desde a ultima leitura -- SEMPRE
                                        // exato, independe do buffer (alimenta a media)
    uint32_t ultimoPeriodoMicros;       // periodo entre os 2 ultimos pulsos
    uint32_t microsDesdeUltimoPulso;    // para o timeout de calmaria
    const uint32_t* timestamps;         // timestamps (micros) dos pulsos da janela
    uint32_t totalTimestamps;           // quantos cabem no buffer (cap 320)
    uint32_t descartadosPorBuffer;      // pulsos alem do buffer -- so degrada a
                                        // resolucao da rajada, nunca a media
};

struct Amostra {
    float avgSpeedMs;
    float gustSpeedMs;
};

// Unica fronteira entre o modulo de hardware e o de matematica: recebe a
// janela crua, devolve o par pronto pro payload da Fase 5.
Amostra calcularAmostra(const JanelaDePulsos& janela);
```

- [ ] **Step 4: Acrescentar a `src/medicao.cpp`**

```cpp
Amostra calcularAmostra(const JanelaDePulsos& janela)
{
    Amostra amostra;

    const float freqHz = janela.contagem / 10.0f;
    amostra.avgSpeedMs = 1.319f * freqHz / PULSOS_POR_VOLTA;

    amostra.gustSpeedMs = amostra.avgSpeedMs;  // placeholder ate a Task 5

    return amostra;
}
```

- [ ] **Step 5: Rodar e confirmar que passa**

Run: `pio test -e native`
Expected: 8 testes, `0 Failures`

- [ ] **Step 6: Commit**

```bash
git add include/medicao.h src/medicao.cpp test/test_medicao/test_medicao.cpp
git commit -m "feat: velocidade media a partir da contagem de pulsos"
```

---

## Task 5: Rajada — janela deslizante de 3 s

**Files:**
- Modify: `src/medicao.cpp`
- Modify: `test/test_medicao/test_medicao.cpp`

**Interfaces:**
- Consumes: `JanelaDePulsos`, `Amostra`, `calcularAmostra` (Task 4) — substitui o placeholder da rajada
- Produces: `calcularAmostra` completo (média + rajada de verdade)

- [ ] **Step 1: Escrever os testes que falham**

Acrescentar ao `test/test_medicao/test_medicao.cpp`:

```cpp
void test_amostra_vento_constante_rajada_igual_media(void) {
    // 10 pulsos espacados uniformemente a cada 1s, ao longo de 10s.
    uint32_t timestamps[10];
    for (int i = 0; i < 10; i++) {
        timestamps[i] = (uint32_t)(i + 1) * 1000000;  // micros
    }

    JanelaDePulsos janela = {};
    janela.contagem = 10;
    janela.timestamps = timestamps;
    janela.totalTimestamps = 10;

    Amostra amostra = calcularAmostra(janela);

    // vento constante: a rajada (pico de qualquer janela de 3s) coincide
    // com a media geral
    TEST_ASSERT_FLOAT_WITHIN(0.05f, amostra.avgSpeedMs, amostra.gustSpeedMs);
}

void test_amostra_rajada_no_meio_supera_media(void) {
    // 10 pulsos espacados a 1s (vento fraco e constante), exceto entre
    // 3s e 6s onde vem uma rajada: pulsos concentrados a cada ~0.3s.
    uint32_t timestamps[16];
    int i = 0;
    timestamps[i++] = 1000000;
    timestamps[i++] = 2000000;
    timestamps[i++] = 3000000;
    timestamps[i++] = 4000000;
    // rajada: pulsos mais frequentes entre 4s e 6s
    timestamps[i++] = 4300000;
    timestamps[i++] = 4600000;
    timestamps[i++] = 4900000;
    timestamps[i++] = 5200000;
    timestamps[i++] = 5500000;
    timestamps[i++] = 5800000;
    timestamps[i++] = 6000000;
    timestamps[i++] = 7000000;
    timestamps[i++] = 8000000;
    timestamps[i++] = 9000000;
    timestamps[i++] = 10000000;

    JanelaDePulsos janela = {};
    janela.contagem = (uint32_t)i;
    janela.timestamps = timestamps;
    janela.totalTimestamps = (uint32_t)i;

    Amostra amostra = calcularAmostra(janela);

    TEST_ASSERT_TRUE(amostra.gustSpeedMs > amostra.avgSpeedMs);

    // Pico esperado, contado a mao (janela desliza em cada indice `fim`,
    // pega o `inicio` mais a esquerda com t[fim]-t[inicio] <= 3.0s):
    // o maximo acontece em fim=10 (t=6.0s) e fim=11 (t=7.0s), ambos com
    // inicio=indice de t=3.0s ou t=4.0s respectivamente -- 8 PERIODOS
    // (9 pontos) em 3.0s = 8/3 = 2,667 Hz. Contamos periodos (pontos-1),
    // nao pontos, propositalmente -- ver o comentario do algoritmo no
    // Step 3 desta task.
    const float freqPicoEsperada = 8.0f / 3.0f;
    const float gustEsperado = 1.319f * freqPicoEsperada / PULSOS_POR_VOLTA;
    TEST_ASSERT_FLOAT_WITHIN(0.1f, gustEsperado, amostra.gustSpeedMs);
}

void test_amostra_buffer_no_limite_nao_le_fora(void) {
    // Buffer cheio (320, o cap real) com mais pulsos do que ele
    // comporta -- contagem (350) fica maior que totalTimestamps (320).
    // Confirma que calcularAmostra nao le fora do array (o teste
    // simplesmente rodar sem crash ja prova isso) e que a MEDIA usa a
    // contagem real, nao o buffer capado.
    uint32_t timestamps[320];
    for (int j = 0; j < 320; j++) {
        timestamps[j] = (uint32_t)(j + 1) * 31250;  // ~32 Hz, span ~10s
    }

    JanelaDePulsos janela = {};
    janela.contagem = 350;              // contagem real > buffer
    janela.timestamps = timestamps;
    janela.totalTimestamps = 320;       // capado
    janela.descartadosPorBuffer = 30;   // 350 - 320

    Amostra amostra = calcularAmostra(janela);

    // nao trava, nao le fora do array (o proprio teste rodar sem crash
    // ja e a prova), e a media usa CONTAGEM real, nao o buffer capado
    const float freqHzEsperada = 350 / 10.0f;
    const float avgEsperado = 1.319f * freqHzEsperada / PULSOS_POR_VOLTA;
    TEST_ASSERT_FLOAT_WITHIN(0.05f, avgEsperado, amostra.avgSpeedMs);
}
```

E os `RUN_TEST`:

```cpp
    RUN_TEST(test_amostra_vento_constante_rajada_igual_media);
    RUN_TEST(test_amostra_rajada_no_meio_supera_media);
    RUN_TEST(test_amostra_buffer_no_limite_nao_le_fora);
```

- [ ] **Step 2: Rodar e confirmar que falha**

Run: `pio test -e native`
Expected: FAIL — `test_amostra_rajada_no_meio_supera_media` falha porque o placeholder atual (`gustSpeedMs = avgSpeedMs`) não bate com o pico

- [ ] **Step 3: Implementar a janela deslizante em `src/medicao.cpp`**

Substituir a implementação de `calcularAmostra` (troca a linha do placeholder):

```cpp
namespace {

constexpr uint32_t JANELA_RAJADA_MICROS = 3'000'000;  // 3s

float calcularPicoDeRajada(const uint32_t* timestamps, uint32_t total)
{
    if (total < 2) {
        return 0.0f;  // precisa de pelo menos 2 pontos pra ter um periodo
    }

    float picoHz = 0.0f;
    uint32_t inicio = 0;

    // Dois ponteiros: para cada "fim" avancando, retrocede "inicio" ate
    // caber na janela de 3s. Pulsos dentro de [fim - 3s, fim] contam.
    for (uint32_t fim = 0; fim < total; fim++) {
        while (timestamps[fim] - timestamps[inicio] > JANELA_RAJADA_MICROS) {
            inicio++;
        }

        // PERIODOS entre pulsos, nao pontos: N pontos uniformemente
        // espacados cobrem N-1 periodos. Contar pontos superestimaria
        // a frequencia sistematicamente -- 10 pulsos a 1s dariam "rajada"
        // de 1,33 Hz contra uma media real de 1,0 Hz, quebrando a
        // invariante "vento constante = rajada igual a media".
        const uint32_t periodosNaJanela = fim - inicio;
        if (periodosNaJanela == 0) {
            continue;  // um unico ponto na janela, sem periodo formado
        }

        const float freqHz = periodosNaJanela / 3.0f;
        if (freqHz > picoHz) {
            picoHz = freqHz;
        }
    }

    return picoHz;
}

}  // namespace

Amostra calcularAmostra(const JanelaDePulsos& janela)
{
    Amostra amostra;

    const float freqMediaHz = janela.contagem / 10.0f;
    amostra.avgSpeedMs = 1.319f * freqMediaHz / PULSOS_POR_VOLTA;

    const float picoHz = calcularPicoDeRajada(janela.timestamps, janela.totalTimestamps);
    amostra.gustSpeedMs = 1.319f * picoHz / PULSOS_POR_VOLTA;

    // A rajada nunca pode ser menor que a media -- se o buffer nao tinha
    // timestamps suficientes (janela vazia mas contagem>0, caso
    // teoricamente impossivel mas defensivo), nao deixa gust < avg.
    if (amostra.gustSpeedMs < amostra.avgSpeedMs) {
        amostra.gustSpeedMs = amostra.avgSpeedMs;
    }

    return amostra;
}
```

- [ ] **Step 4: Rodar e confirmar que passa**

Run: `pio test -e native`
Expected: 11 testes, `0 Failures`

- [ ] **Step 5: Commit**

```bash
git add src/medicao.cpp test/test_medicao/test_medicao.cpp
git commit -m "feat: rajada por janela deslizante de 3s, testada nativamente"
```

---

## Task 6: Velocidade instantânea e calmaria

**Files:**
- Modify: `include/medicao.h`
- Modify: `src/medicao.cpp`
- Modify: `test/test_medicao/test_medicao.cpp`

**Interfaces:**
- Consumes: `JanelaDePulsos`, `periodoParaVelocidadeMs` (Task 2)
- Produces: `velocidadeInstantaneaMs(const JanelaDePulsos&) -> float`

- [ ] **Step 1: Escrever os testes que falham**

Acrescentar ao `test/test_medicao/test_medicao.cpp`:

```cpp
void test_instantanea_pulso_recente_usa_periodo(void) {
    JanelaDePulsos janela = {};
    janela.ultimoPeriodoMicros = 475000;       // 10 km/h
    janela.microsDesdeUltimoPulso = 100000;    // 100ms atras -- recente

    float velocidade = velocidadeInstantaneaMs(janela);

    TEST_ASSERT_FLOAT_WITHIN(0.05f, 2.77f, velocidade);
}

void test_instantanea_calmaria_apos_10s_sem_pulso(void) {
    JanelaDePulsos janela = {};
    janela.ultimoPeriodoMicros = 475000;       // implicaria 10 km/h
    janela.microsDesdeUltimoPulso = 10000001;  // 1 micros acima de 10s

    float velocidade = velocidadeInstantaneaMs(janela);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, velocidade);
}

void test_instantanea_exatamente_no_limite_ainda_conta(void) {
    JanelaDePulsos janela = {};
    janela.ultimoPeriodoMicros = 475000;
    janela.microsDesdeUltimoPulso = 10000000;  // exatamente 10s

    float velocidade = velocidadeInstantaneaMs(janela);

    TEST_ASSERT_FLOAT_WITHIN(0.05f, 2.77f, velocidade);
}
```

E os `RUN_TEST`:

```cpp
    RUN_TEST(test_instantanea_pulso_recente_usa_periodo);
    RUN_TEST(test_instantanea_calmaria_apos_10s_sem_pulso);
    RUN_TEST(test_instantanea_exatamente_no_limite_ainda_conta);
```

- [ ] **Step 2: Rodar e confirmar que falha**

Run: `pio test -e native`
Expected: FAIL — `velocidadeInstantaneaMs` não declarada

- [ ] **Step 3: Acrescentar a `include/medicao.h`**

```cpp
// So para uso local (Serial), NAO vai no payload da Fase 5 -- o backend
// so aceita avg_speed_ms/gust_speed_ms por janela de 10s, nao um valor
// "agora". Aplica o timeout de calmaria: retorna 0 se
// microsDesdeUltimoPulso > 10s, mesmo que ultimoPeriodoMicros implique
// velocidade diferente de zero.
float velocidadeInstantaneaMs(const JanelaDePulsos& janela);
```

- [ ] **Step 4: Acrescentar a `src/medicao.cpp`**

```cpp
namespace {
constexpr uint32_t TIMEOUT_CALMARIA_MICROS = 10'000'000;  // 10s
}  // namespace

float velocidadeInstantaneaMs(const JanelaDePulsos& janela)
{
    if (janela.microsDesdeUltimoPulso > TIMEOUT_CALMARIA_MICROS) {
        return 0.0f;
    }

    return periodoParaVelocidadeMs(janela.ultimoPeriodoMicros);
}
```

- [ ] **Step 5: Rodar e confirmar que passa**

Run: `pio test -e native`
Expected: 14 testes, `0 Failures`

- [ ] **Step 6: Commit**

```bash
git add include/medicao.h src/medicao.cpp test/test_medicao/test_medicao.cpp
git commit -m "feat: velocidade instantanea com timeout de calmaria"
```

---

## Task 7: `anemometro.{h,cpp}` — ISR e captura de hardware

**Files:**
- Create: `include/anemometro.h`
- Create: `src/anemometro.cpp`

**Interfaces:**
- Consumes: `gravarTimestampSeCouber` (Task 3), `JanelaDePulsos` (Task 4)
- Produces: `iniciarAnemometro()`, `lerEZerarJanela() -> JanelaDePulsos`

⚠️ **Esta task não tem teste automatizado.** Toca hardware real (ISR, GPIO,
`attachInterrupt`) — não compila no `env:native` porque inclui `<Arduino.h>`.
A verificação possível agora é compilar para `esp32dev` e revisar com cuidado;
a prova de que a ISR funciona sob interrupção real só vem com o dupont
(`specs/pendencias-hardware.md`).

- [ ] **Step 1: Implementar `include/anemometro.h`**

```cpp
#pragma once

#include "medicao.h"

// Configura o GPIO e anexa a interrupcao. Chamar uma vez no setup().
void iniciarAnemometro();

// Copia atomica do estado acumulado E ZERA os acumuladores. Cada
// chamada fecha uma janela -- por isso deve ser chamada a cada 10s,
// nunca em outro ritmo (e o contrato que o backend espera).
JanelaDePulsos lerEZerarJanela();
```

- [ ] **Step 2: Implementar `src/anemometro.cpp`**

```cpp
#include "anemometro.h"

#include <Arduino.h>

namespace {

constexpr uint8_t  PINO_SENSOR       = 25;
constexpr uint32_t DEBOUNCE_MICROS   = 5000;  // 5ms
constexpr uint32_t CAPACIDADE_BUFFER = 320;

// Todo o estado tocado pela ISR precisa ser volatile: o compilador nao
// pode assumir que o loop() ve o mesmo valor que a ISR escreveu.
volatile uint32_t bufferTimestamps[CAPACIDADE_BUFFER];
volatile uint32_t totalTimestamps    = 0;
volatile uint32_t descartadosBuffer  = 0;
volatile uint32_t contagemTotal      = 0;
volatile uint32_t ultimoPulsoAceito  = 0;
volatile uint32_t penultimoPulsoAceito = 0;
volatile bool      houvePulso        = false;

void IRAM_ATTR isrPulso()
{
    const uint32_t agora = micros();

    // Debounce: descarta qualquer transicao dentro de 5ms do ultimo
    // pulso ACEITO (nao do ultimo evento bruto).
    if (houvePulso && (agora - ultimoPulsoAceito) < DEBOUNCE_MICROS) {
        return;
    }

    if (houvePulso) {
        penultimoPulsoAceito = ultimoPulsoAceito;
    } else {
        // Primeiro pulso desde o boot: nao existe "penultimo" de
        // verdade. Usar o mesmo instante zera o periodo calculado --
        // periodoParaVelocidadeMs ja trata periodo=0 como 0 m/s. Sem
        // isso, o periodo seria "tempo desde o boot ate agora", um
        // numero sem nenhum sentido fisico.
        penultimoPulsoAceito = agora;
    }
    ultimoPulsoAceito = agora;
    houvePulso = true;

    // gravarTimestampSeCouber e pura (medicao.cpp) -- nao aloca, nao
    // bloqueia, seguro dentro de ISR mesmo operando sobre o buffer
    // volatile via cast.
    const bool gravou = gravarTimestampSeCouber(
        const_cast<uint32_t*>(bufferTimestamps), CAPACIDADE_BUFFER,
        totalTimestamps, agora);

    if (gravou) {
        totalTimestamps++;
    } else {
        descartadosBuffer++;
    }

    contagemTotal++;
}

}  // namespace

void iniciarAnemometro()
{
    pinMode(PINO_SENSOR, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PINO_SENSOR), isrPulso, FALLING);
}

JanelaDePulsos lerEZerarJanela()
{
    JanelaDePulsos janela = {};

    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);

    janela.contagem = contagemTotal;
    janela.ultimoPeriodoMicros = houvePulso
        ? (ultimoPulsoAceito - penultimoPulsoAceito)
        : 0;
    janela.microsDesdeUltimoPulso = houvePulso
        ? (micros() - ultimoPulsoAceito)
        : UINT32_MAX;  // nunca houve pulso -- calmaria total
    janela.totalTimestamps = totalTimestamps;
    janela.descartadosPorBuffer = descartadosBuffer;

    contagemTotal = 0;
    totalTimestamps = 0;
    descartadosBuffer = 0;

    portEXIT_CRITICAL(&mux);

    // timestamps aponta pro buffer estatico -- valido ate a proxima
    // chamada desta funcao, que so acontece 10s depois.
    janela.timestamps = const_cast<const uint32_t*>(bufferTimestamps);

    return janela;
}
```

- [ ] **Step 3: Compilar para o ESP32 real**

Run: `pio run -e esp32dev`
Expected: `SUCCESS`, sem warning novo relacionado a `anemometro.cpp`

- [ ] **Step 4: Confirmar que os testes nativos continuam passando**

Run: `pio test -e native`
Expected: 14 testes, `0 Failures` — `anemometro.cpp` não deve ter quebrado nada, porque `env:native` nem o compila (só `medicao.cpp`)

- [ ] **Step 5: Commit**

```bash
git add include/anemometro.h src/anemometro.cpp
git commit -m "feat: captura de pulsos via ISR, GPIO 25"
```

---

## Task 8: Integração em `main.cpp` + modo de calibração

**Files:**
- Modify: `src/main.cpp`
- Modify: `platformio.ini`
- Modify: `README.md`

**Interfaces:**
- Consumes: `iniciarAnemometro`, `lerEZerarJanela` (Task 7), `calcularAmostra`, `velocidadeInstantaneaMs` (Tasks 4-6)

- [ ] **Step 1: Acrescentar o ambiente de calibração ao `platformio.ini`**

```ini
; --- Modo de calibracao -------------------------------------
; So para descobrir pulsos/volta quando o dupont chegar.
; make flash ENV=calibracao
[env:calibracao]
extends = env:esp32dev
build_flags = ${env:esp32dev.build_flags} -D MODO_CALIBRACAO
```

- [ ] **Step 2: Ler o `src/main.cpp` atual antes de editar**

Use a ferramenta de leitura para ver o conteúdo exato antes de editar — o arquivo já tem `iniciarWifi()`/`atualizarWifi()` da Fase 4 e não deve ser reescrito do zero, só estendido.

- [ ] **Step 3: Adicionar os includes e a lógica de medição**

No topo do arquivo, junto dos includes existentes:

```cpp
#include "anemometro.h"
#include "medicao.h"
```

No `setup()`, depois da chamada a `iniciarWifi()` existente — **exceto se `MODO_CALIBRACAO` estiver definido**, caso em que o `setup()`/`loop()` inteiros seguem um caminho separado, minimalista:

```cpp
#ifdef MODO_CALIBRACAO

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("=== Ventos CPS | Modo de calibracao ===");
    Serial.println("Gire o rotor e observe a contagem. Ctrl+C para sair.");
    iniciarAnemometro();
}

void loop()
{
    static uint32_t ultimoPrint = 0;
    const uint32_t agora = millis();

    if (agora - ultimoPrint < 1000) {
        return;
    }
    ultimoPrint = agora;

    JanelaDePulsos janela = lerEZerarJanela();
    static uint32_t totalAcumulado = 0;
    totalAcumulado += janela.contagem;

    Serial.printf("[calibracao] pulsos_neste_segundo=%lu total_acumulado=%lu\n",
                  (unsigned long) janela.contagem, (unsigned long) totalAcumulado);
}

#else

// setup() e loop() normais continuam abaixo, com a integracao da
// medicao acrescentada ao loop() existente.

#endif
```

Dentro do `setup()` do caminho normal (fora do `#ifdef`), acrescentar a chamada, junto de `iniciarWifi()`:

```cpp
    iniciarAnemometro();
```

Dentro do `loop()` normal, acrescentar — reaproveitando o padrão não-bloqueante de `millis()` que o Wi-Fi já usa, com o mesmo intervalo de 10s que fecha a janela do backend:

```cpp
    static uint32_t ultimaMedicao = 0;
    if (agora - ultimaMedicao >= 10000) {
        ultimaMedicao = agora;

        JanelaDePulsos janela = lerEZerarJanela();
        const float instantanea = velocidadeInstantaneaMs(janela);
        const Amostra amostra = calcularAmostra(janela);

        Serial.printf("[medicao] agora=%.2f avg=%.2f gust=%.2f m/s\n",
                      instantanea, amostra.avgSpeedMs, amostra.gustSpeedMs);
    }
```

Ajuste a variável `agora` para reaproveitar a que o loop de status do Wi-Fi já declara (ou declare a sua própria com `millis()`, se a estrutura do arquivo não tiver uma variável de escopo compartilhado) — o objetivo é não bloquear e não duplicar a leitura de `millis()` desnecessariamente.

- [ ] **Step 4: Compilar os dois ambientes**

Run: `pio run -e esp32dev`
Expected: `SUCCESS`

Run: `pio run -e calibracao`
Expected: `SUCCESS`

- [ ] **Step 5: Gravar o firmware normal no ESP32 real e observar**

Run: `pio run -e esp32dev -t upload`

Depois, ler o monitor serial por ~30s (o ESP32 já tem `secrets.h` configurado
da Fase 4). Sem o sensor conectado, o GPIO 25 fica em pull-up estável — o
comportamento esperado é `agora=0.00 avg=0.00 gust=0.00` indefinidamente,
e o status de Wi-Fi da Fase 4 continuando a aparecer normalmente. Isso NÃO é
um teste vazio: prova que a integração não quebrou o Wi-Fi (regressão) e que
o caminho `contagem=0` (calmaria completa) não trava nem gera lixo.

Expected: linhas `[status] ... wifi=conectado ...` (Fase 4) intercaladas com
`[medicao] agora=0.00 avg=0.00 gust=0.00 m/s` a cada 10s, sem crash, por pelo
menos 30s seguidos.

- [ ] **Step 6: Documentar no `README.md`**

Acrescentar à tabela de comandos existente (não recriar a tabela, só
adicionar linhas):

```markdown
| `make test ENV=native` | Roda os testes de `medicao.cpp` no Mac, sem placa |
| `make flash ENV=calibracao` | Grava o modo de calibração (contagem de pulsos/volta) |
```

E uma seção nova, próxima de onde o `secrets.h` já é documentado:

```markdown
## Calibração de pulsos/volta

Quando o conector dupont chegar, antes de tudo:

\`\`\`bash
make flash ENV=calibracao
\`\`\`

Gire o rotor exatamente 10 voltas devagar e leia `total_acumulado` no
monitor serial. 10 → 1 pulso/volta (já assumido em todo o projeto);
20 → 2 pulsos/volta (trocar `PULSOS_POR_VOLTA` em `include/medicao.h`
para `2.0f`). Detalhes: [`specs/pendencias-hardware.md`](specs/pendencias-hardware.md).
```

- [ ] **Step 7: Commit**

```bash
git add src/main.cpp platformio.ini README.md
git commit -m "feat: integra medicao ao loop principal e ao modo de calibracao"
```

---

## Cobertura da spec

| Requisito de `specs/medicao.md` | Task |
|---|---|
| Separação hardware/matemática, `env:native` | 1 |
| `periodoParaVelocidadeMs`, `PULSOS_POR_VOLTA` isolado | 2 |
| Buffer capado, sem overflow silencioso | 3 |
| Velocidade média por contagem | 4 |
| Rajada por janela deslizante de 3s | 5 |
| `descartadosPorBuffer` não afeta a média | 5 |
| Velocidade instantânea + timeout de calmaria de 10s | 6 |
| ISR: `IRAM_ATTR`, debounce 5ms, GPIO 25, `FALLING` | 7 |
| `lerEZerarJanela` com seção crítica | 7 |
| Integração no loop de 10s, sem bloquear | 8 |
| Modo de calibração embutido, reaproveitando a ISR real | 8 |

## Fora de escopo deste plano

Envio HTTP, buffer offline, watchdog, OTA (Fase 5); troca `FALLING`/`RISING`
para PC817 (Fase 6); qualquer verificação que dependa do sensor físico
(pendente do dupont, ver `specs/pendencias-hardware.md`).
