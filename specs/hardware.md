# Hardware

Notação de confiança usada neste documento:

- ✅ **Confirmado** pelo fabricante ou pela documentação oficial
- 🧮 **Derivado** por cálculo a partir de dados confirmados
- 💡 **Recomendação** técnica (decisão nossa, não imposição do fabricante)
- ❓ **Hipótese** — ainda precisa ser verificada

---

## 1. Anemômetro

Anemômetro de conchas, fornecedor WRF Comercial.

### Especificações ✅

| Item | Valor |
|---|---|
| Conchas | alumínio, 50 mm de diâmetro, 2 unidades |
| Suporte de fixação | alumínio, 200 mm |
| Diâmetro total do rotor | **210 mm** |
| Eixo | rolamento, livre de manutenção |
| Sensor | magnético lacrado, ímã de neodímio (reed switch) |
| Cabo | manga, **6 metros**, já montado de fábrica |
| Velocidade de partida | **0,7 km/h** |
| Velocidade máxima | **+135 km/h** |
| Fixação | abraçadeiras laterais com porcas |
| Peso | 198 g |
| Ambiente | resistente a intempéries |

### Interface elétrica ✅

Dois fios (vermelho e branco), **contato seco, sem polaridade**. Não há eletrônica interna nem alimentação — é um interruptor mecânico puro, acionado pelo ímã a cada passagem.

Confirmado pelo fabricante: *"Seria por pulso através de um reed switch, 3v3"*.

### Geometria 🧮

O "suporte de 200 mm" é o braço de fixação ao mastro, **não** o braço das conchas. O rotor tem 210 mm de diâmetro total, portanto **raio de 105 mm** — que bate exatamente com o `radius = 105` do programa de exemplo do fabricante. Isso confirma que aquele código foi escrito para este produto.

Centro da concha fica a ~80 mm do eixo; borda externa a 105 mm. O fabricante usa o raio **externo** na fórmula — não é rigoroso fisicamente, mas o desvio está absorvido no fator empírico da calibração.

### Pulsos por volta ❓ — **pendência aberta**

Não confirmado. O rotor tem 2 conchas; pode ter 1 ou 2 ímãs.

**Impacto:** erro de 2× em todas as leituras, silencioso.

**Como resolver:** contagem direta na Fase 2 — girar o rotor 10 voltas à mão e ler o contador no Serial Monitor. 10 pulsos → 1/volta; 20 pulsos → 2/volta.

Todo o restante deste documento assume **1 pulso por volta**. Se a medição indicar 2, a constante de calibração cai pela metade.

---

## 2. Calibração

### Fórmula do fabricante ✅

```
v (m/s) = (4 · π · raio_mm · RPM / 60) / 1000
```

Com raio = 105 mm. O fator `4π` (em vez de `2π`) embute um **fator de anemômetro = 2** — relação empírica entre a velocidade do vento e a velocidade linear das conchas.

### Constante simplificada 🧮

Substituindo o raio e convertendo RPM para frequência de pulsos:

> **v (m/s) = 1,319 × f (Hz)**
> **v (km/h) = 4,750 × f (Hz)**
>
> A partir do período: **v (m/s) = 1319 / T(ms)**

*(válido para 1 pulso por volta)*

### Tabela de operação 🧮

| Vento | m/s | RPM | Frequência | Período |
|---|---|---|---|---|
| 0,7 km/h *(partida)* | 0,19 | 8,8 | 0,15 Hz | **6,8 s** |
| 5 km/h | 1,4 | 63 | 1,05 Hz | 950 ms |
| 10 km/h | 2,8 | 126 | 2,1 Hz | 475 ms |
| 20 km/h | 5,6 | 253 | 4,2 Hz | 237 ms |
| 40 km/h | 11,1 | 505 | 8,4 Hz | 119 ms |
| 60 km/h | 16,7 | 758 | 12,6 Hz | 79 ms |
| 100 km/h | 27,8 | 1263 | 21,1 Hz | 47 ms |
| 135 km/h *(máx)* | 37,5 | 1705 | 28,4 Hz | **35 ms** |

Faixa dinâmica de ~200:1. Essa tabela é a origem de duas restrições de projeto — ver [firmware.md](firmware.md).

### Validação da exatidão ❓

A fórmula é do fabricante, mas não recebemos tabela de calibração de referência. A exatidão absoluta é desconhecida. Se importar, o caminho é comparar com uma estação meteorológica próxima ao longo de alguns dias.

---

## 3. Microcontrolador

Placa DevKit ESP32-WROOM-32 ✅

| Item | Valor |
|---|---|
| Chip | ESP32-WROOM-32, dual core Xtensa LX6 @ 240 MHz |
| SRAM | 520 KB |
| Flash | 4 MB |
| Rádio | Wi-Fi 802.11 b/g/n 2,4 GHz + BLE 4.2 |
| USB-Serial | CP2102 |
| Alimentação | USB-C 5 V ou pino VIN |
| Deep sleep | 5 µA |

O macOS 15 tem driver nativo para o CP2102 — não instalar driver da Silicon Labs.

### Escolha de GPIO 💡

**Sensor: GPIO 25.**

Pinos a evitar:

| Pino | Motivo |
|---|---|
| GPIO 0 | Strapping de boot — nível baixo no reset entra em modo bootloader. Um reed fechado aqui impede a placa de subir. |
| GPIO 2 | Strapping + LED onboard |
| GPIO 12, 15 | Strapping (tensão da flash / silenciamento de log) |
| GPIO 6–11 | Ligados à flash SPI interna |
| GPIO 34–39 | Somente entrada, **sem pull-up interno** — inviabiliza o esquema do reed |

⚠️ O exemplo do fabricante usa `attachInterrupt(0, ...)`, que no Arduino UNO significa "pino 2". No ESP32 isso aponta para o **GPIO 0**. Portar literalmente quebra o boot.

O LED onboard usa GPIO 2 sem problema: o strapping só é lido enquanto o pino é entrada, durante o reset. Como saída, depois do boot, é seguro.

---

## 4. Ligação

### Fase 2 a 5 — bancada, ligação direta 💡

```
Anemômetro fio A ──────── ESP32 GPIO 25
Anemômetro fio B ──────── ESP32 GND
```

Sem componentes. Usa o **pull-up interno** (~45 kΩ) do ESP32:

| Reed | Pino | Leitura |
|---|---|---|
| aberto | segurado em 3,3 V pelo pull-up | **HIGH** |
| fechado | curto ao GND vence o resistor | **LOW** |

Corrente pelo contato: ~73 µA. Configuração: `pinMode(25, INPUT_PULLUP)`, interrupção em **`FALLING`**.

Seguro porque o sensor é contato seco confirmado — não há tensão vindo do cabo.

### Fase 6 — instalação definitiva, com PC817 💡

O fabricante forneceu esquema com optoacoplador. **Não é obrigatório** em 3,3 V (o esquema é genérico, atende também 5 V e 12 V), mas é recomendado para a instalação externa.

```
fio A ──┬── 470R ── [PC817 pino 1]
        │           [PC817 pino 2] ── GND
fio B ──┴── 3V3

[PC817 pino 4] ── 3V3
[PC817 pino 3] ──┬── GPIO 25
                 └── 10k ── GND
```

**Motivo:** 6 m de cabo no alto de uma caixa d'água funcionam como antena. Raio caindo nas proximidades induz picos; poeira e chuva geram estática. Com o optoacoplador, o cabo externo nunca toca o GPIO — quem queima é um componente de R$ 2, não a placa.

Não há isolação galvânica real neste desenho (os dois lados compartilham GND e 3V3), mas a proteção sacrificial vale.

⚠️ **A lógica inverte:** com PC817 o reed fechado gera **HIGH** → interrupção em `RISING`. Manter essa escolha atrás de um `#define` desde o início.

### Lista de componentes — Fase 6

O fabricante **não vende** a placa montada.

| Item | Qtd |
|---|---|
| PC817 | 2 *(um reserva — é o sacrificial)* |
| Soquete DIP-4 | 1 |
| Resistor 470 Ω | 1 |
| Resistor 10 kΩ | 1 |
| Prensa-cabo | 1 |

---

## 5. Instalação

- **Mastro na vertical.** Anemômetro de conchas é omnidirecional — não existe lado "virado para o vento". O que importa é o eixo estar vertical e as conchas girarem na horizontal.
- **Distância máxima de 6 m** entre anemômetro e caixa do ESP32 — é o cabo que veio, e não vamos emendar. Emenda em ambiente externo é fonte clássica de infiltração. Medir no local antes de definir os pontos de fixação.
- **Prensa-cabo obrigatório** na entrada da caixa hermética. Furar e passar o cabo direto anula a vedação.
- 💡 **Furo na face inferior da caixa**, nunca em cima nem na lateral. Água escorre.
- Alimentação: carregador USB de celular (5 V) via USB-C.

### Vida útil do reed ❓

Reed switch é peça de desgaste. Com vento médio de 10 km/h são ~180 mil acionamentos por dia (~66 milhões/ano).

Atenuante: a corrente é baixíssima (73 µA na ligação direta, 4,5 mA com o PC817), regime de *dry circuit*, sem arco elétrico no contato — o cenário de maior longevidade possível.

Vida útil esperada não informada pelo fabricante. Vale perguntar em algum momento, sem urgência.
