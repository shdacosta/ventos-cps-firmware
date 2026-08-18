# Ventos CPS — Firmware

Firmware do ESP32 da estação de medição de vento. Lê os pulsos de um anemômetro de conchas, calcula velocidade e rajada, e envia para a API por Wi-Fi.

📖 **O que é e por quê:** [`specs/README.md`](specs/README.md)
🔧 **Como rodar:** este documento.

O **servidor** — API, banco e SPA — vive no repositório `ventos-cps`.

---

## Comandos

O PlatformIO fica na raiz, então basta rodar `make` daqui. `make` sozinho lista tudo.

```bash
make flash
```

Compila, grava e abre o monitor serial — o comando do dia a dia.

| Comando | O que faz |
|---|---|
| `make build` | Compila |
| `make upload` | Compila e grava |
| `make monitor` | Monitor serial *(sair: Ctrl+C)* |
| **`make flash`** | **Compila + grava + monitor** |
| `make size` | Uso de Flash e RAM |
| `make clean` | Apaga o build |
| `make fullclean` | Apaga build + cache de dependências |
| `make erase` | Apaga **toda** a Flash do ESP32 |
| `make ports` | Lista portas seriais |
| `make libs` | Bibliotecas instaladas |
| `make update` | Atualiza plataforma/libs dentro dos limites do `platformio.ini` |
| `make check` | Análise estática (lint) |
| `make test` | Testes do PlatformIO |

A porta é detectada sozinha. Havendo mais de um dispositivo conectado, force a certa:

```bash
make flash PORT=/dev/cu.usbserial-0001
```

⚠️ `make erase` apaga a partição NVS junto — é lá que ficam as credenciais de Wi-Fi salvas a partir da Fase 4. Não é um `clean` mais forte; use só para sair de boot loop por estado corrompido.

---

## Credenciais de Wi-Fi

O build **falha** sem isso — `secrets.h` não é opcional, o código faz `#include "secrets.h"`.

```bash
cp include/secrets.h.example include/secrets.h
```

Edite `include/secrets.h` com o SSID e a senha da sua rede. Esse arquivo está no `.gitignore` e nunca é commitado — só o `.example` (sem credenciais reais) fica versionado.

---

## Setup inicial

```bash
# 1. Extensão do PlatformIO
code --install-extension platformio.platformio-ide

# 2. pio disponível no terminal (opcional para quem usa só o make)
echo 'export PATH="$HOME/.platformio/penv/bin:$PATH"' >> ~/.zshrc
```

Reinicie o VS Code após instalar a extensão — ele baixa o PlatformIO Core (~200 MB) na primeira abertura.

**Driver USB:** o macOS 15 tem suporte nativo ao CP2102. Não instalar driver da Silicon Labs.

---

## Solução de problemas

| Sintoma | Causa | Correção |
|---|---|---|
| Caracteres estranhos contínuos no monitor | Baud divergente entre firmware e monitor | `make monitor` usa o `monitor_speed` do `platformio.ini` |
| Poucos caracteres estranhos **só no reset** | Bootloader da ROM em velocidade própria | Normal, ignorar |
| `Could not exclusively lock port` | Monitor aberto em outro terminal | Fechar o outro monitor (Ctrl+C) |
| Placa não aparece em `make ports` | Cabo USB-C só de energia, sem dados | Trocar o cabo antes de suspeitar da placa |
| Upload falha por ruído | `upload_speed` alto demais | Baixar para `115200` no `platformio.ini` |

Porta esperada no macOS: `/dev/cu.usbserial-0001`. Conferir com `make ports`.

---

## Estrutura

```
ventos-cps-firmware/
├── CLAUDE.md              convenções e forma de trabalho
├── Makefile               atalhos do PlatformIO
├── platformio.ini         configuração do build
├── include/
│   ├── secrets.h.example  template de credenciais (versionado)
│   ├── secrets.h          credenciais reais (git-ignored, você cria)
│   └── wifi_gerenciado.h  interface do módulo de Wi-Fi
├── src/
│   ├── main.cpp           orquestração: setup(), loop()
│   └── wifi_gerenciado.cpp  conexão, reconexão, NTP
└── specs/                 hardware, requisitos de firmware, pendências
```

---

## Hardware

| Item | Valor |
|---|---|
| Placa | ESP32-WROOM-32 DevKit (CP2102, 4 MB Flash) |
| Anemômetro | conchas, reed switch, contato seco, cabo de 6 m |
| GPIO do sensor | **25** *(a partir da Fase 2)* |
| Calibração | `v (m/s) = 1,319 × f (Hz)` |
| Debounce | **5 ms** — valores maiores cortam rajadas em silêncio |

Detalhes, pinos a evitar e esquema de ligação: [`specs/hardware.md`](specs/hardware.md).

---

## Status

**Fase 1 concluída** — ambiente, compilação, gravação e monitor validados.

**Fase 4 em andamento** — conexão Wi-Fi não-bloqueante, reconexão com backoff exponencial e sincronização de hora via NTP implementadas (`src/wifi_gerenciado.cpp`). Adiantada fora de ordem porque não depende do anemômetro.

**Fase 2 bloqueada** — o anemômetro não pode ser ligado até chegar o conector dupont. Checklist do que confirmar assim que chegar: [`specs/pendencias-hardware.md`](specs/pendencias-hardware.md).

Roadmap completo: [`specs/README.md`](specs/README.md#roadmap).
