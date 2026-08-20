# Pendências de hardware — para quando o conector dupont chegar

Log vivo. Cada item aqui é uma confirmação física que só pode acontecer com o
anemômetro ligado de verdade. Nada disso pode ser resolvido em código.

---

## 1. Pulsos por volta ❓ — a única incógnita técnica que resta no projeto

**Por quê:** o rotor tem 2 conchas; pode ter 1 ou 2 ímãs no reed switch. Se for
2 e o firmware assumir 1, **toda leitura de vento sai com o dobro do valor
real** — e o erro é silencioso, sem nenhum aviso de que algo está errado.

**Como resolver, passo a passo:**

1. Ligar o anemômetro direto no ESP32: fio A → GPIO 25, fio B → GND. Sem
   componente nenhum (contato seco confirmado, seguro). Ver [`hardware.md`](hardware.md#4-ligação).
2. Gravar um firmware simples que conta interrupções em `FALLING` no GPIO 25
   e imprime o total no Serial Monitor.
3. Girar o rotor **exatamente 10 voltas completas**, devagar, à mão.
4. Ler o contador:
   - **10** → 1 pulso por volta (o que todo o projeto já assume)
   - **20** → 2 pulsos por volta (a constante de calibração cai pela metade)
   - Outro número → sinal de repique (bounce) não filtrado — ajustar o
     debounce antes de repetir o teste

**O que muda dependendo do resultado:** a constante `v (m/s) = 1,319 × f (Hz)`
documentada em `hardware.md` está calculada para 1 pulso/volta. Se o teste
indicar 2, essa constante precisa ser recalculada (metade do valor) antes da
Fase 3 (medição) começar.

---

## 2. Debounce — validar contra o valor teórico

**Já decidido:** 5 ms (ver `firmware.md` §1.2). Calculado para dar folga de 7×
até o limite do sensor (135 km/h = período de 35 ms entre pulsos).

**Confirmar na prática:** ao girar o rotor devagar no teste do item 1, contar
se aparecem pulsos "extras" perto de cada passagem do ímã (sinal de bounce
vazando). Se sim, o debounce de 5 ms pode estar curto demais para *este*
reed switch específico — cada peça tem um tempo de repique um pouco diferente.

---

## 3. Cabo e prensa-cabo

- **Comprimento do cabo manga:** confirmado em 6 m de fábrica. Medir a
  distância real entre o ponto de fixação do anemômetro e a caixa hermética
  do ESP32 **antes** de fixar qualquer coisa — não há folga para emenda.
- **Prensa-cabo:** ainda não comprado. Necessário para a entrada do cabo na
  caixa hermética sem quebrar a vedação (Fase 6).

---

## 4. Placa do PC817 (Fase 6, instalação definitiva)

Fabricante não vende montada. Lista de componentes já fechada em
`hardware.md` §4: PC817 ×2 (um reserva), soquete DIP-4, resistor 470 Ω,
resistor 10 kΩ. Comprar com calma — só entra em uso na instalação final, não
na bancada.

---

## 5. ISR do anemômetro não é totalmente cache-safe por padrão ⚠️ — reavaliado, risco aceito por ora

**Por quê:** `isrPulso()` (em `src/anemometro.cpp`) é `IRAM_ATTR`, mas o framework
usado neste projeto tem `CONFIG_ARDUINO_ISR_IRAM` desligado por padrão — o que
significa que, hoje, qualquer escrita em flash (o Wi-Fi da Fase 4 já faz isso
via NVS ao salvar/reconectar) mascara a interrupção do GPIO enquanto a escrita
dura. O efeito prático é perda **silenciosa** de pulsos — sem crash, sem erro,
sem log — durante essas janelas.

`gravarTimestampSeCouber()` (chamada de dentro da ISR) foi marcada `IRAM_ATTR`
para o build do ESP32 (guardado por `#ifdef ARDUINO`, sem afetar o build
nativo) justamente para deixar a cadeia de chamada da ISR pronta para o dia em
que `CONFIG_ARDUINO_ISR_IRAM` for ligado — sem essa marcação, ligar a flag
faria a ISR saltar para código em flash com o cache desligado, o que crasha.
Mas isso resolve só metade do problema: mesmo com a cadeia toda em IRAM, a
flag em si continua desligada por padrão, então a perda silenciosa de pulsos
durante escrita de flash **continua acontecendo hoje**.

**Reavaliação feita na Fase 5 (2026-08-20), sem precisar de hardware:**

Fase 5 (telemetria) chegou e **não** agravou o risco como este item temia —
o buffer de telemetria ficou em RAM (`telemetria.md §9`), não em flash/NVS,
então a frequência de escrita em flash continua sendo só o NVS do Wi-Fi ao
reconectar, igual antes.

Foi possível avançar a auditoria da cadeia de chamada da ISR que este item
pedia — sem nenhum sensor conectado, só com o toolchain já instalado:

- ✅ **Confirmado**, compilando o firmware real (`pio run -e esp32dev`) e
  inspecionando o mapa de símbolos do ELF gerado (`xtensa-esp32-elf-nm`/
  `readelf`): `isrPulso()` (`0x400813f4`) e `gravarTimestampSeCouber()`
  (`0x4008906c`) caem dentro de `.iram0.text` (`0x40080404`–`0x40095773`),
  como esperado — mas `micros()`, chamada logo na primeira linha de
  `isrPulso()` (`src/anemometro.cpp:41`), está em `0x400dbb98`, dentro de
  `.flash.text` (`0x400d0020` em diante). **Fora da IRAM, hoje.**
- ✅ **Confirmado**, lendo o código-fonte do framework
  (`cores/esp32/esp32-hal-misc.c` e `esp32-hal.h`): `micros()` é declarada
  `unsigned long ARDUINO_ISR_ATTR micros()`, e `ARDUINO_ISR_ATTR` é definida
  como `IRAM_ATTR` quando `CONFIG_ARDUINO_ISR_IRAM` está ligado, e como nada
  (vazio) quando está desligado — exatamente o mesmo mecanismo condicional
  que motivou marcar `gravarTimestampSeCouber()` como `IRAM_ATTR` neste
  projeto. `micros()` estar em flash hoje não é acaso nem descuido: é
  consequência direta e prevista da flag estar desligada.
- 💡 **Recomendação/inferência** (não testada ao vivo): como `micros()` já
  usa esse mesmo mecanismo condicional do framework, ligar
  `CONFIG_ARDUINO_ISR_IRAM` moveria `micros()` para IRAM automaticamente,
  junto com `isrPulso()`/`gravarTimestampSeCouber()` — não seria necessário
  nenhum patch manual nessa chamada especificamente.
- ❓ **Hipótese ainda em aberto** (é a parte que realmente falta pra fechar
  a auditoria): não verificado se o registro do handler em si —
  `attachInterrupt()` (`esp32-hal-gpio.c`) e o serviço de ISR de GPIO do
  ESP-IDF que ele usa por baixo (`gpio_install_isr_service`/
  `gpio_isr_handler_add`) — também respeita esse mesmo mecanismo quando a
  flag está ligada, ou se tem alguma parte do próprio dispatch de
  interrupção do framework que ficaria em flash independente da flag. Isso
  também é verificável por software (leitura do código-fonte do ESP-IDF já
  instalado, igual ao que foi feito acima), só não foi feito ainda.

**Decisão registrada:** manter a Opção 2 (aceitar o risco, documentado) por
ora — não ligar `CONFIG_ARDUINO_ISR_IRAM`. Motivo: a Fase 5 não piorou o
risco (buffer ficou em RAM), a auditoria completa da cadeia ainda tem uma
lacuna real (item ❓ acima), e ligar essa flag sem validar em hardware de
verdade arrisca um crash silencioso bem pior que a perda de pulso que já
existe hoje — não vale trocar um risco conhecido e pequeno por um risco
maior e não testado, ainda mais com o dispositivo fora de bancada agora. Se
algum dia quiserem revisitar: o próximo passo concreto (também sem
hardware) é ler `esp32-hal-gpio.c`/`gpio.c` do ESP-IDF pra fechar a lacuna
❓ acima; só depois disso faria sentido testar a flag ligada, e mesmo assim
só com o dispositivo em bancada, monitorando crash.

---

## 6. Janela de medição assumia 10s fixos ✅ corrigido em código — falta só confirmar com vento real

**O que era:** `calcularAmostra()` (`medicao.cpp`) fazia `freqHz = janela.contagem / 10.0f` — divisão por 10 fixa, assumindo que a janela sempre durava exatamente 10s. Isso era verdade até a Fase 5: o `loop()` nunca bloqueava por mais que isso. A partir da Fase 5, `tentarEnviarLotes()` pode bloquear (POST HTTP com timeout de 8s, até `MAX_LOTES_POR_CICLO=3` lotes seguidos) — se um ciclo de envio demorasse mais de 10s no total, a próxima janela fechava "atrasada", com mais de 10s de pulsos acumulados, mas a fórmula continuava dividindo por 10 — `avg_speed_ms` saía inflado (e `gust_speed_ms` seguia junto, pelo clamp que impede rajada menor que a média).

**Correção aplicada** (achado da revisão final da Fase 5): `calcularAmostra()` agora recebe `duracaoJanelaMs` (a duração real da janela, medida em `main.cpp` via `millis()` antes de zerar o timer) em vez de assumir `10.0f` fixo. A rajada não precisava de mudança — o algoritmo dela já opera sobre os timestamps brutos numa janela deslizante de 3s, sem assumir duração total nenhuma. Testado nativamente: `test_amostra_janela_mais_longa_que_10s_nao_infla_media` prova que uma janela de 20s com o dobro de pulsos dá a mesma velocidade média de uma janela normal de 10s (o cenário exato do bug), e `test_amostra_duracao_zero_nao_divide_por_zero` cobre o caso de borda defensivo.

**O que ainda falta, quando o sensor estiver conectado de verdade:** confirmar com vento real que `avg_speed_ms`/`gust_speed_ms` continuam corretos mesmo quando um ciclo de envio demora mais que 10s (forçar isso na prática: sinal de Wi-Fi fraco, ou backend respondendo devagar de propósito, enquanto o rotor gira). A matemática já está provada nativamente; falta só a confirmação de integração com o sensor físico, que nenhum teste ao vivo cobriu ainda porque `contagem` sempre foi 0 até agora.

---

## 7. Contagem de reconexões de Wi-Fi ❓ — definição operacional depende de confirmação ao vivo

**O que é:** `wifiContagemReconexoes()` (`wifi_gerenciado.cpp`) incrementa
quando `WiFi.status()` volta a `WL_CONNECTED` depois de ter saído desse
estado ao menos uma vez (não conta a conexão inicial do boot). A lógica
da transição foi extraída para `wifi_transicao.{h,cpp}`
(`avaliarTransicaoWifi()`) e tem cobertura nativa — ver nota abaixo; só a
casca que toca `WiFi.h` real (`wifi_gerenciado.cpp`) continua sem teste
nativo possível, mesma categoria de `anemometro.cpp`.

**Por quê é uma hipótese, não um fato confirmado:** a definição de
"reconexão de verdade" depende de `WiFi.status()` nunca "piscar" por um
único tick de `loop()` sem ter havido queda real de sinal — algo que só
dá pra confirmar com rádio de verdade, não por revisão estática. Achado
mais específico da revisão final: um "piscar" desse tipo não só seria
**contado errado** como reconexão — ele **dispara de fato** a tentativa
de reconexão que acaba sendo contada (`proximaTentativaEm` já expirado
no momento do piscar), então o efeito é auto-alimentado, não só um erro
de contagem isolado.

**Como confirmar, quando o dispositivo estiver disponível:**

1. **Caso positivo:** derrubar o Wi-Fi de propósito (desligar o roteador
   alguns segundos) e confirmar no monitor serial que `contadorReconexoes`
   incrementa exatamente uma vez por queda, mesmo que várias tentativas de
   reconexão falhem antes de a rede voltar.
2. **Caso negativo — não pular este passo:** deixar o dispositivo com Wi-Fi
   estável por um período longo (algumas horas, não só minutos) e confirmar
   que o contador **não sobe** nesse intervalo. Sem esse segundo teste, um
   incremento causado por piscar de sinal (sem queda real) passaria
   despercebido — é exatamente o cenário que tornaria o número enganoso
   para diagnóstico remoto, o propósito original do requisito.

**Nota (extração da máquina de estados):** a lógica de decisão em si
(quando incrementar o contador, resetar o backoff, tratar um "piscar"
como reconexão) foi extraída para `wifi_transicao.{h,cpp}` e agora tem
prova automatizada nativa, incluindo um teste dedicado pro cenário exato
do "piscar" descrito acima
(`test_piscar_de_status_conta_como_reconexao_comportamento_conhecido`,
em `test/test_wifi_transicao/test_wifi_transicao.cpp`), que documenta
esse comportamento como conhecido/aceito, não como bug. O que ainda
falta — e mantém este item ❓ — é só a confirmação de que o rádio real
da ESP32 se comporta como o modelo assume (não "pisca" sem queda real);
a lógica que reage a esse comportamento, essa já está provada.

---

## Como usar este arquivo

Risque o item conforme for confirmado, com a data e o resultado. Quando o
item 1 (pulsos por volta) for resolvido, atualizar:

- `hardware.md` — trocar o ❓ pela confirmação ✅ e ajustar a constante se for 2/volta
- `specs/README.md` — Fase 2 sai de 🔴 bloqueada
