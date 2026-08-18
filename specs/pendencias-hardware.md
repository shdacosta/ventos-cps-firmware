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

## Como usar este arquivo

Risque o item conforme for confirmado, com a data e o resultado. Quando o
item 1 (pulsos por volta) for resolvido, atualizar:

- `hardware.md` — trocar o ❓ pela confirmação ✅ e ajustar a constante se for 2/volta
- `specs/README.md` — Fase 2 sai de 🔴 bloqueada
