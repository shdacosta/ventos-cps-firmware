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
| `make test ENV=native` | Roda os testes de `medicao.cpp`, `telemetria.cpp` e `wifi_transicao.cpp` no Mac, sem placa (41 casos) |
| `make flash ENV=calibracao` | Grava o modo de calibração (contagem de pulsos/volta) |
| `make upload ENV=ota PORT=<ip>` | Grava por Wi-Fi, sem cabo USB (dispositivo já rodando na rede) |

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

## Telemetria e OTA

Além do SSID/senha, `include/secrets.h` também define:

| Constante | Para quê |
|---|---|
| `INGEST_TOKEN` | Token do backend (`Authorization: Bearer`) — mesmo valor de `INGEST_TOKEN` na configuração do servidor |
| `INGEST_URL` | URL completa do endpoint de ingestão (`http://<ip-do-backend>:8000/api/v1/ingest`) |
| `OTA_SENHA` | Senha exigida para gravar por Wi-Fi (`make upload ENV=ota`) |

Pra gravar via OTA, exporte a senha no shell antes (mesmo valor de `OTA_SENHA` no `secrets.h`):

```bash
export OTA_SENHA=sua-senha-aqui
make upload ENV=ota PORT=<ip>
```

`platformio.ini` lê essa variável de ambiente (`${sysenv.OTA_SENHA}`) na hora do upload — sem ela exportada, a gravação falha com `Authentication Failed` mesmo que a senha esteja certa no firmware.

---

## Calibração de pulsos/volta

Quando o conector dupont chegar, antes de tudo:

```bash
make flash ENV=calibracao
```

Gire o rotor exatamente 10 voltas devagar e leia `total_acumulado` no
monitor serial. 10 → 1 pulso/volta (já assumido em todo o projeto);
20 → 2 pulsos/volta (trocar `PULSOS_POR_VOLTA` em `include/medicao.h`
para `2.0f`). Detalhes: [`specs/pendencias-hardware.md`](specs/pendencias-hardware.md).

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
| Upload OTA para no meio (ex.: trava em ~18%) | Sinal Wi-Fi fraco (RSSI baixo) | Tentar de novo — costuma completar na segunda tentativa; esta estação já tem sinal fraco conhecido perto do roteador (ver Status) |

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
│   ├── wifi_gerenciado.h  Wi-Fi, NTP, relógio em unix time
│   ├── anemometro.h       ISR, GPIO — captura de pulsos
│   ├── medicao.h          matemática pura — velocidade, rajada, calmaria
│   ├── telemetria.h       lógica pura — buffer, retry, payload JSON
│   ├── envio.h            HTTP de verdade — envia telemetria pro backend
│   └── watchdog.h         reset automático se o loop travar
├── src/
│   ├── main.cpp           orquestração: setup(), loop()
│   ├── wifi_gerenciado.cpp
│   ├── anemometro.cpp
│   ├── medicao.cpp
│   ├── telemetria.cpp
│   ├── envio.cpp
│   └── watchdog.cpp
├── test/
│   ├── test_medicao/      testes nativos de medicao.cpp
│   └── test_telemetria/   testes nativos de telemetria.cpp
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

**Fases 1, 2, 3, 4 e 5 concluídas.**

- **Fase 1** — ambiente, compilação, gravação e monitor validados.
- **Fase 2+3** — captura de pulsos via ISR (GPIO 25) e cálculo de velocidade/rajada/calmaria, com matemática testada nativamente. Verificação da ISR sob interrupção real (pulsos/volta, debounce) segue pendente do conector dupont — checklist em [`specs/pendencias-hardware.md`](specs/pendencias-hardware.md).
- **Fase 4** — Wi-Fi não-bloqueante, reconexão com backoff exponencial, hora via NTP. Testada ao vivo. Sinal fraco medido perto do roteador (-82 a -84 dBm), relevante para a instalação da Fase 6.
- **Fase 5** — envio HTTP em lote (buffer de telemetria de 4h em RAM; os buffers de transmissão do payload JSON e do lote de saída ficam alocados em heap, para não apertar a região estática do linker), watchdog e OTA via `ArduinoOTA`, testados ao vivo contra o backend real. A primeira tentativa de gravação por OTA parou em ~18% (sinal fraco, mesmo RSSI baixo da Fase 4); a segunda, sem nenhuma mudança, completou (`[SUCCESS]`, que já inclui verificação de checksum pelo próprio protocolo `espota`) — ver linha correspondente em "Solução de problemas".

**Fase 6 (instalação física, PC817)** — ainda não iniciada.

Roadmap completo: [`specs/README.md`](specs/README.md#roadmap).
