# Spec — Captura de pulsos e medição (Fases 2+3)

**Design aprovado em 2026-08-18.** Construído antes do conector dupont chegar — ver
a nota de confiança em cada seção sobre o que é verificável agora e o que só se
prova com o sensor real.

Pré-requisitos já decididos: [hardware.md](hardware.md) (GPIO, ligação, calibração),
[firmware.md §1-2](firmware.md#1-restrições-que-vêm-do-sensor) (regras da ISR,
tabela de métodos por grandeza), [pendencias-hardware.md](pendencias-hardware.md)
(o que falta confirmar fisicamente).

---

## 1. Objetivo

Transformar os pulsos do reed switch em duas grandezas por janela de 10 s:
velocidade média e rajada — no formato exato que o backend já aceita
(`avg_speed_ms`, `gust_speed_ms`, ver `backend/README.md` no repo do servidor).

A janela de 10 s não é escolha nova: é o contrato já implementado no backend
(uma amostra a cada 10 s, lote a cada 60 s).

---

## 2. Decisão estruturante: separar hardware de matemática

| Módulo | Toca hardware? | Como se verifica agora |
|---|---|---|
| `anemometro.{h,cpp}` | Sim — ISR, GPIO, `attachInterrupt` | Só por revisão + calibração ao vivo quando o dupont chegar |
| `medicao.{h,cpp}` | Não — funções puras | **Testes nativos** (PlatformIO `env:native`), rodando no Mac, sem placa |

Essa fronteira existe para que a parte mais fácil de errar silenciosamente — a
matemática da rajada, o timeout de calmaria, a conversão período→velocidade —
tenha prova automatizada real, mesmo sem hardware. A parte que não dá para
testar sem sensor (a ISR em si: debounce sob interrupção real, timing de
`IRAM_ATTR`) fica reduzida ao mínimo possível, e frágil só nela.

---

## 3. `anemometro.h` — captura

```cpp
void iniciarAnemometro();

struct JanelaDePulsos {
    uint32_t contagem;                 // pulsos desde a ultima leitura -- SEMPRE exato,
                                        // independe do buffer (e o que alimenta a media)
    uint32_t ultimoPeriodoMicros;       // periodo entre os 2 ultimos pulsos
    uint32_t microsDesdeUltimoPulso;    // para o timeout de calmaria
    const uint32_t* timestamps;         // timestamps (micros) dos pulsos da janela
    uint32_t totalTimestamps;           // quantos cabem no buffer (cap 320)
    uint32_t descartadosPorBuffer;      // pulsos alem dos 320 -- so degrada a
                                        // resolucao da rajada, nunca a media
};

// Copia atomica do estado acumulado E ZERA os acumuladores. Cada chamada
// fecha uma janela -- e por isso que ela deve ser chamada a cada 10s, nunca
// em outro ritmo.
JanelaDePulsos lerEZerarJanela();
```

### Regras da ISR (já definidas em firmware.md, repetidas aqui por serem o contrato deste módulo)

- `IRAM_ATTR`, anexada uma vez no `setup()` via `digitalPinToInterrupt(PINO_SENSOR)`
- Debounce de 5 ms comparando `micros()` contra o último pulso aceito, dentro da própria ISR
- Grava o timestamp no buffer circular, incrementa contador, sai — zero ponto flutuante, zero `Serial`, zero alocação
- `lerEZerarJanela()` faz a cópia dentro de `portENTER_CRITICAL`/`portEXIT_CRITICAL` — nunca dentro da ISR

### Buffer de timestamps

**320 posições** (`uint32_t`, ~1,3 KB). Dimensionado para o pior caso: 135 km/h =
28,4 Hz → até 284 pulsos em 10 s. Se a janela encher (só possível acima do
limite físico do sensor), os pulsos excedentes incrementam `descartadosPorBuffer`
em vez de escrever fora do array — nunca um overflow silencioso de índice.

**Importante:** `contagem` (o contador simples, incrementado na ISR) nunca é
afetado pelo buffer cheio — só a lista de `timestamps` é limitada. Ou seja, a
**média** continua exata mesmo no cenário extremo de estourar o buffer; só a
**rajada** perderia um pouco de resolução, porque ela precisa dos timestamps
individuais, não só da contagem total.

### GPIO e edge

`GPIO 25`, `INPUT_PULLUP`, interrupção em `FALLING` (ligação direta de bancada).
A troca para `RISING` da Fase 6 (com PC817) fica atrás de um `#define
LOGICA_INVERTIDA`, não decidida agora — ver `hardware.md §4`.

---

## 4. `medicao.h` — matemática pura, testada nativamente

```cpp
// Trocar aqui quando o dupont confirmar pulsos/volta (pendencias-hardware.md #1).
constexpr float PULSOS_POR_VOLTA = 1.0f;

// v(m/s) = 1319 / (T(ms) * PULSOS_POR_VOLTA).
//
// Por que multiplicar T por PULSOS_POR_VOLTA, nao dividir o resultado:
// com 2 pulsos/volta, o periodo OBSERVADO entre pulsos e a METADE do
// periodo de uma rotacao completa (2 eventos por volta, nao 1) -- entao
// o periodo real de rotacao e T_observado * PULSOS_POR_VOLTA. Aplicar a
// constante sobre esse periodo corrigido da a velocidade certa direto,
// sem precisar de uma segunda divisao depois.
float periodoParaVelocidadeMs(uint32_t periodoMicros);

// So para uso local (Serial), NAO vai no payload da Fase 5 -- o backend
// so aceita avg_speed_ms/gust_speed_ms por janela de 10s, nao um valor
// "agora". Aplica o timeout de calmaria: retorna 0 se
// microsDesdeUltimoPulso > 10s, mesmo que ultimoPeriodoMicros implique
// velocidade diferente de zero.
float velocidadeInstantaneaMs(const JanelaDePulsos& janela);

struct Amostra {
    float avgSpeedMs;
    float gustSpeedMs;
};

// Unica fronteira entre o modulo de hardware e o de matematica: recebe a
// janela crua, devolve o par pronto pro payload. E esta funcao que os
// testes nativos exercitam com timestamps sinteticos.
Amostra calcularAmostra(const JanelaDePulsos& janela);
```

### Velocidade média

**Não** reaproveita `periodoParaVelocidadeMs` — aquela função parte de um único
período (um pulso), esta parte de uma frequência (contagem ÷ tempo). Cálculo
próprio, mesma constante:

```
freqHz = contagem / 10.0
avgSpeedMs = 1.319 * freqHz / PULSOS_POR_VOLTA
```

(equivalente à fórmula de `hardware.md §2`, com o ajuste de `PULSOS_POR_VOLTA`
aplicado do mesmo jeito que em `periodoParaVelocidadeMs` — mais pulsos por
volta superestimam a frequência real, então se divide por `PULSOS_POR_VOLTA`.)

Contagem é o método correto para média — já justificado em `firmware.md §2`.

### Rajada — tradução do "máximo da média móvel de 3 s" (padrão WMO) para evento discreto

Um anemômetro comum dá leitura contínua; este dá eventos. A tradução: aplicar
o mesmo princípio da média (pulsos ÷ tempo), mas numa **janela de 3 s deslizante
dentro da janela de 10 s**, pegando o pico.

Algoritmo: janela deslizante de dois ponteiros sobre os timestamps ordenados —
O(n), sem alocação, sem ponto flutuante até o passo final de conversão. Para
cada posição da janela, `freqHz = pulsos_na_janela_de_3s / 3.0`, convertida
para m/s pela mesma fórmula da seção anterior (`1.319 * freqHz /
PULSOS_POR_VOLTA`); guarda o máximo observado.

**❓ Hipótese, não confirmação do fabricante.** É uma aproximação razoável do
padrão WMO adaptada a um sensor de pulso — não uma medição contínua real. Vale
documentar isso se algum dia a exatidão da rajada for questionada.

### Calmaria

Implementada dentro de `velocidadeInstantaneaMs`: se `microsDesdeUltimoPulso >
10_000_000` (10 s), retorna 0 mesmo que `ultimoPeriodoMicros` implicasse
velocidade diferente de zero. Sem isso, o vento parando deixaria a leitura
instantânea "congelada" no último período calculado, minutos atrás.

A **média da janela** (`Amostra.avgSpeedMs`), nesse mesmo cenário, já é
naturalmente 0 (zero pulsos ÷ 10 s) — o timeout de calmaria não precisa (nem
deve) ser aplicado em `calcularAmostra`; ele é específico da leitura
instantânea.

---

## 5. Integração em `main.cpp`

- `setup()`: chama `iniciarAnemometro()` (além do já existente `iniciarWifi()`)
- `loop()`: a cada 10 s (mesmo padrão não-bloqueante de `millis()` já usado
  para o Wi-Fi), chama `lerEZerarJanela()` uma vez, e a partir da mesma
  `JanelaDePulsos` calcula `velocidadeInstantaneaMs()` e `calcularAmostra()`,
  imprimindo os três no Serial: `agora=X.XX avg=Y.YY gust=Z.ZZ`
- **Não entra neste ciclo:** envio HTTP do payload, buffer offline, contador
  de sequência — isso é Fase 5, consumindo a struct `Amostra` daqui

---

## 6. Modo de calibração — embutido, reaproveitando o código real

```ini
[env:calibracao]
extends = env:esp32dev
build_flags = ${env:esp32dev.build_flags} -D MODO_CALIBRACAO
```

Novo alvo `make calibrar` = grava com esse ambiente + abre o monitor.

Dentro de `main.cpp`, um `#ifdef MODO_CALIBRACAO` substitui o `setup()`/`loop()`
normal por uma versão mínima: só `iniciarAnemometro()` (sem Wi-Fi — reduz
ruído e tempo de boot), imprimindo a contagem bruta acumulada a cada 1 s.

**Por que embutido em vez de sketch separado:** a calibração passa a rodar o
mesmo caminho de ISR e debounce do firmware real, em vez de uma versão
simplificada à parte que poderia se comportar diferente. Quando o dupont
chegar: `make calibrar`, girar o rotor 10 vezes, ler o número, ajustar
`PULSOS_POR_VOLTA` se for 2, voltar para `make flash` normal.

---

## 7. Testes

### Nativos (`env:native`, PlatformIO + Unity, roda no Mac sem placa)

Cobertura mínima de `medicao.cpp`:

- `periodoParaVelocidadeMs`: valores da tabela de operação de `hardware.md`
  (0,7 km/h → período 6,8 s; 135 km/h → período 35 ms), conferindo contra a
  constante `1319/T(ms)`
- `calcularAmostra` com janela vazia (`contagem=0`): `avg=0`
- `calcularAmostra` com pulsos uniformemente espaçados (vento constante):
  `avg` e `gust` devem coincidir
- `calcularAmostra` com uma rajada sintética no meio da janela (pulsos mais
  frequentes por 3 s, depois voltando ao normal): `gust > avg`, e `gust`
  bate com o pico calculado à mão
- `velocidadeInstantaneaMs` com `microsDesdeUltimoPulso` acima de 10 s: retorna
  0 mesmo com `ultimoPeriodoMicros` implicando velocidade não-zera — prova a
  calmaria isoladamente
- `velocidadeInstantaneaMs` com pulso recente: bate com
  `periodoParaVelocidadeMs(ultimoPeriodoMicros)`
- Buffer no limite (321+ timestamps sintéticos): `descartadosPorBuffer > 0`,
  `totalTimestamps` não passa de 320, `contagem` continua exata (não afetada
  pelo descarte), e a função não trava nem lê fora do array
- `PULSOS_POR_VOLTA = 2.0`: mesma tabela de período, valores pela metade —
  prova que a constante realmente propaga para o resultado

### No hardware real (não coberto por este ciclo)

ISR sob interrupção real, debounce contra repique físico, e a própria
contagem de pulsos/volta — ficam pendentes do dupont, registrados em
[pendencias-hardware.md](pendencias-hardware.md).

---

## 8. Fora de escopo deste ciclo

- Envio HTTP, buffer offline, contador de sequência, watchdog, OTA — Fase 5
- Troca de lógica `FALLING`/`RISING` para o PC817 — Fase 6
- Qualquer teste que dependa do sensor físico — bloqueado pelo dupont

## 9. Riscos conhecidos

| Risco | Mitigação |
|---|---|
| `PULSOS_POR_VOLTA` errado (❓ pendência aberta) | Constante isolada, um lugar só para trocar; testes nativos provam que a propagação funciona nos dois valores |
| Rajada é aproximação, não WMO literal | Documentado como hipótese; revisitar só se a exatidão da rajada virar requisito real |
| ISR nunca testada sob interrupção real até o dupont chegar | Modo de calibração exercita o caminho real assim que possível, antes do firmware "de produção" ser considerado pronto |
| Buffer de 320 pulsos podendo, em teoria, não bastar se o sensor girar acima do limite do fabricante | Descarte contado, sem overflow silencioso; 320 já tem ~13% de folga sobre o pior caso físico documentado |
