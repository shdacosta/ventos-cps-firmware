// ============================================================
// Ventos Campinas - FASE 4
// Objetivo: conectar no Wi-Fi, reconectar sozinho se cair, e manter o
// relogio sincronizado via NTP -- tudo sem bloquear o loop().
// Anemometro ainda nao entra: Fase 2 bloqueada pelo conector dupont.
// ============================================================

#include <Arduino.h>

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

void setup()
{
    Serial.begin(115200);
    delay(500);  // margem para o monitor serial conectar e nao
                 // perder as primeiras linhas

    pinMode(LED_PIN, OUTPUT);

    Serial.println();
    Serial.println("=== Ventos Campinas | Fase 4 ===");

    iniciarWifi();
}

void loop()
{
    atualizarWifi();

    const uint32_t agora = millis();

    // Subtracao antes da comparacao: imune ao estouro do millis()
    // aos 49 dias.
    if (agora - ultimoStatus < INTERVALO_STATUS_MS) {
        return;
    }
    ultimoStatus = agora;

    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    imprimirStatus();
}
