// ============================================================
// Ventos Campinas - FASE 4 + integracao da medicao (Fase 3)
// Objetivo: conectar no Wi-Fi, reconectar sozinho se cair, manter o
// relogio sincronizado via NTP, e fechar uma janela de medicao a
// cada 10s -- tudo sem bloquear o loop().
// Verificacao com sensor real ainda pendente: Fase 2 bloqueada pelo
// conector dupont (specs/pendencias-hardware.md). Ate la, o GPIO 25
// fica em pull-up estavel e a janela chega sempre com contagem=0.
// ============================================================

#include <Arduino.h>

#include "anemometro.h"
#include "medicao.h"
#include "wifi_gerenciado.h"

// LED onboard da DevKit. GPIO2 e "strapping pin" (participa da
// decisao de boot), mas isso so vale enquanto ele e ENTRADA no
// reset. Como SAIDA, depois do boot, e seguro.
constexpr uint8_t  LED_PIN             = 2;
constexpr uint32_t INTERVALO_STATUS_MS = 5000;

uint32_t ultimoStatus = 0;

void imprimirStatus()
{
    Serial.printf("[status] uptime=%lus heap=%lu",
                  (unsigned long) (millis() / 1000),
                  (unsigned long) ESP.getFreeHeap());

    if (wifiConectado()) {
        Serial.printf(" wifi=conectado rssi=%ddBm ip=%s hora=%s",
                      wifiRssiDbm(), wifiIp().c_str(), horaAtualFormatada().c_str());
    } else {
        Serial.print(" wifi=desconectado");
    }

    Serial.println();
}

#ifdef MODO_CALIBRACAO

// Caminho minimalista: so a ISR do anemometro, sem Wi-Fi. Existe
// unicamente para descobrir PULSOS_POR_VOLTA girando o rotor a mao
// (specs/pendencias-hardware.md #1). Ver README.md secao
// "Calibracao de pulsos/volta".

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

// setup() e loop() normais: Wi-Fi da Fase 4 com a medicao da Fase 3
// integrada, cada uma no seu proprio intervalo nao-bloqueante.

void setup()
{
    Serial.begin(115200);
    delay(500);  // margem para o monitor serial conectar e nao
                 // perder as primeiras linhas

    pinMode(LED_PIN, OUTPUT);

    Serial.println();
    Serial.println("=== Ventos Campinas | Fase 4 ===");

    iniciarWifi();
    iniciarAnemometro();
}

void loop()
{
    atualizarWifi();

    const uint32_t agora = millis();

    // Janela de medicao: fecha a cada 10s, no mesmo ritmo que o
    // backend espera por amostra (specs/firmware.md). Checada em
    // toda volta do loop() -- nao pode depender do intervalo de
    // status abaixo, que e independente (5s) e pode mudar sem
    // afetar a medicao.
    static uint32_t ultimaMedicao = 0;
    if (agora - ultimaMedicao >= 10000) {
        ultimaMedicao = agora;

        JanelaDePulsos janela = lerEZerarJanela();
        const float instantanea = velocidadeInstantaneaMs(janela);
        const Amostra amostra = calcularAmostra(janela);

        Serial.printf("[medicao] agora=%.2f avg=%.2f gust=%.2f m/s\n",
                      instantanea, amostra.avgSpeedMs, amostra.gustSpeedMs);
    }

    // Subtracao antes da comparacao: imune ao estouro do millis()
    // aos 49 dias.
    if (agora - ultimoStatus < INTERVALO_STATUS_MS) {
        return;
    }
    ultimoStatus = agora;

    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    imprimirStatus();
}

#endif
