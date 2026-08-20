# ============================================================
# Ventos CPS - Firmware
# ============================================================
# O PlatformIO vive na raiz deste repositorio, entao nenhum alvo
# precisa de -d: basta rodar make daqui.
#
# Escrito para macOS: os alvos de porta serial assumem /dev/cu.*.
#
# Variaveis sobrescreviveis:
#   make flash PORT=/dev/cu.usbserial-0001
#   make build ENV=esp32dev

# Default de ENV depende do alvo: build/flash/etc. querem a placa de
# verdade (esp32dev); test quer native. esp32dev nunca e a escolha certa
# para test -- platformio.ini marca test_ignore = * nesse ambiente de
# proposito (test/test_medicao define seu proprio main(), que colidiria
# com o setup()/loop() do Arduino ao linkar). Sem este ajuste, "make test"
# sem argumento coleta 0 casos contra esp32dev e sai com exit 0 -- um
# "sucesso" que nao testou nada. ENV=x continua funcionando para qualquer
# alvo, isto so muda o valor assumido quando ninguem passa nada.
ifeq ($(MAKECMDGOALS),test)
ENV ?= native
else
ENV ?= esp32dev
endif
PORT ?=

# O instalador do PlatformIO poe o binario em ~/.platformio/penv/bin,
# que nem sempre esta no PATH.
PIO ?= $(shell command -v pio 2>/dev/null || echo "$(HOME)/.platformio/penv/bin/pio")

# Sem aspas de proposito: aqui o word splitting e desejado.
UPLOAD_PORT_FLAG  = $(if $(PORT),--upload-port $(PORT),)
MONITOR_PORT_FLAG = $(if $(PORT),--port $(PORT),)

.PHONY: help build upload monitor flash size clean fullclean erase ports libs update check test

.NOTPARALLEL:

.DEFAULT_GOAL := help

help: ## Lista os comandos disponiveis
	@awk 'BEGIN {FS = ":.*## "} /^##@/ { printf "\n\033[1m%s\033[0m\n", substr($$0, 5); next } /^[a-zA-Z0-9_.-]+:.*## / { printf "  \033[36m%-12s\033[0m %s\n", $$1, $$2 }' $(MAKEFILE_LIST)

##@ Ciclo principal

build: ## Compila o firmware
	$(PIO) run -e $(ENV)

upload: ## Compila e grava no ESP32
	$(PIO) run -e $(ENV) -t upload $(UPLOAD_PORT_FLAG)

monitor: ## Abre o monitor serial (Ctrl+C para sair)
	$(PIO) device monitor -e $(ENV) $(MONITOR_PORT_FLAG)

flash: ## Compila + grava + abre o monitor serial (o mais usado)
	$(PIO) run -e $(ENV) -t upload -t monitor $(UPLOAD_PORT_FLAG)

size: ## Mostra o uso de Flash e RAM do binario compilado
	$(PIO) run -e $(ENV) -t size

##@ Manutencao

clean: ## Apaga os artefatos de build
	$(PIO) run -e $(ENV) -t clean

fullclean: ## Apaga build + cache de dependencias baixadas
	$(PIO) run -e $(ENV) -t fullclean

erase: ## Apaga TODA a Flash do ESP32 (inclusive credenciais salvas)
	$(PIO) run -e $(ENV) -t erase $(UPLOAD_PORT_FLAG)

##@ Ambiente

ports: ## Lista as portas seriais visiveis
	$(PIO) device list

libs: ## Lista as bibliotecas instaladas
	$(PIO) pkg list

update: ## Atualiza plataforma e bibliotecas dentro dos limites do platformio.ini
	$(PIO) pkg update

check: ## Analise estatica (lint do PlatformIO)
	$(PIO) check -e $(ENV)

test: ## Roda os testes nativos (pasta test/, sem placa -- make test ENV=x pra outro ambiente)
	$(PIO) test -e $(ENV) $(UPLOAD_PORT_FLAG)
