# Ventos CPS — Firmware

Firmware do ESP32 da estação de vento: anemômetro → ESP32 → Wi-Fi → API.

Specs em [`specs/`](specs/README.md).

O **servidor** (API, banco e SPA) vive noutro repositório: `ventos-cps`. O contrato do payload de ingestão é definido lá — ver [`specs/firmware.md`](specs/firmware.md).

---

## Contexto sobre quem trabalha aqui

Experiente em desenvolvimento de software. **Iniciante em eletrônica e ESP32.**

Não explicar conceitos básicos de programação (variáveis, funções, loops, estruturas de dados). Explicar sempre, sem assumir conhecimento prévio:

ESP32 · GPIO · pull-up/pull-down · interrupções · debounce · timers · ADC · Wi-Fi embarcado · MQTT · firmware · Flash · PlatformIO · alimentação elétrica

Analogias com desenvolvimento de software são bem-vindas — é o terreno conhecido.

---

## Como conduzir

1. Explicar o **conceito** primeiro
2. Depois mostrar como se aplica **neste projeto**
3. Fornecer código quando necessário — e **explicar o código**
4. Não pular etapas

Desenvolvimento é **incremental**. Cada fase termina com algo funcionando e verificável.

---

## Sempre diferenciar o grau de confiança

Regra permanente, em qualquer afirmação técnica:

- ✅ **Confirmado** pelo fabricante
- 📄 **Confirmado** por documentação oficial
- 💡 **Recomendação** minha
- ❓ **Hipótese** que ainda precisa ser testada

Nunca apresentar hipótese com cara de fato.

---

## Segurança elétrica

- **Não inventar especificações do anemômetro.** Faltou informação elétrica: pedir confirmação ao fabricante ou sugerir teste seguro com multímetro.
- **Não recomendar ligar 5 V ou 3,3 V** em fio de sensor sem certeza da pinagem.
- Havendo risco de danificar o ESP32, ser conservador e dizer explicitamente para **não conectar** antes de confirmar tensão e pinagem.

---

## Objetivo do projeto

Sistema **funcional e confiável, rodando continuamente** — não uma demonstração de bancada.

O dispositivo fica no alto de uma casa de caixa d'água. Isso tem consequências concretas em cada decisão: watchdog, reconexão automática, buffer offline, OTA, tratamento de estouro de contadores, proteção do hardware exposto.

---

## Convenções

- Respostas e documentação em **português brasileiro**
- Commitar direto na `main` — sem feature branches até segunda ordem
- Conventional commits em português, sem menção a IA
- Perguntar antes de criar arquivos novos
- Credenciais nunca versionadas — `include/secrets.h` está no `.gitignore`
