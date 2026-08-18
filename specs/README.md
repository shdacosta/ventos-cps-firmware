# Ventos CPS — Firmware

Firmware do ESP32 da estação de vento. Anemômetro de conchas instalado numa casa de caixa d'água; a placa fica protegida do lado interno, em caixa hermética.

Comandos de build e gravação: [`../README.md`](../README.md).

---

## Índice das specs

| Documento | Escopo |
|---|---|
| [hardware.md](hardware.md) | Anemômetro, ESP32, ligação elétrica, calibração |
| [firmware.md](firmware.md) | Medição, Wi-Fi, telemetria, resiliência |
| [pendencias-hardware.md](pendencias-hardware.md) | Checklist do que confirmar assim que o conector dupont chegar |

---

## Objetivo

Medir e enviar continuamente:

- velocidade instantânea
- velocidade média
- rajadas

Não é demonstração de bancada. O sistema deve rodar por meses sem intervenção, no alto de uma caixa d'água.

---

## Onde este repositório termina

```
Anemômetro (reed switch)
      │  pulsos
      ▼
   ESP32-WROOM-32          ← este repositório
      │  Wi-Fi · HTTP em lote
      ▼
   API (repo ventos-cps)   ← outro repositório
```

O **contrato do payload de ingestão** é definido no repositório do servidor, em
`backend/README.md`. Ele não é duplicado aqui de propósito: duas cópias divergiriam
em silêncio, e o firmware descobriria só quando o servidor começasse a devolver 422.

O que o firmware precisa respeitar desse contrato está resumido em
[firmware.md](firmware.md) — com a origem apontada, nunca reescrita.

---

## Roadmap

| Fase | Entrega | Status |
|---|---|---|
| 1 | Ambiente: PlatformIO + primeiro upload | ✅ concluída |
| 2 | Sensor na bancada — pulsos, pulsos/volta, debounce | 🔴 bloqueada — falta conector |
| 3 | Medição — período, instantânea, média, rajada | ⚪ |
| 4 | Wi-Fi robusto — reconexão, NTP | 🔵 em andamento |
| 5 | Telemetria — HTTP em lote, buffer offline, watchdog, OTA | ⚪ |
| 6 | Hardware definitivo — PC817, caixa hermética, instalação | ⚪ |

As fases 7 (backend) e 8 (SPA) vivem no repositório `ventos-cps`. A 7 está concluída.

Cada fase termina com algo funcionando e verificável.

---

## Pendência que bloqueia a Fase 2

O conector dupont ainda não chegou. Checklist completo do que confirmar assim
que ele chegar — pulsos por volta, debounce, cabo — em
[pendencias-hardware.md](pendencias-hardware.md).

---

## Ambiente de desenvolvimento

- macOS · VS Code · PlatformIO · C/C++
- PlatformIO em vez de Arduino IDE: dependências versionadas e build reproduzível
