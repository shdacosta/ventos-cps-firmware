// ============================================================
// Ventos Campinas - FASE 1
// Objetivo: validar toolchain, gravacao e monitor serial.
// Nenhum hardware externo. Sensor entra na Fase 2.
// ============================================================

#include <Arduino.h>

// LED onboard da DevKit. GPIO2 e "strapping pin" (participa da
// decisao de boot), mas isso so vale enquanto ele e ENTRADA no
// reset. Como SAIDA, depois do boot, e seguro.
// Por isso o LED pode usar GPIO2 e o sensor nao.
constexpr uint8_t  LED_PIN     = 2;
constexpr uint32_t INTERVALO_MS = 2000;

uint32_t contador    = 0;
uint32_t ultimoPisca = 0;

void setup()
{
    Serial.begin(115200);
    delay(500);  // margem para o monitor serial conectar e nao
                 // perder as primeiras linhas

    pinMode(LED_PIN, OUTPUT);

    Serial.println();
    Serial.println("=== Ventos Campinas | Fase 1 ===");
    Serial.printf("Chip .....: %s\n", ESP.getChipModel());
    Serial.printf("Nucleos ..: %d\n", ESP.getChipCores());
    Serial.printf("CPU ......: %lu MHz\n", (unsigned long) ESP.getCpuFreqMHz());
    Serial.printf("Flash ....: %lu MB\n", (unsigned long) (ESP.getFlashChipSize() / (1024UL * 1024UL)));
    Serial.println("Setup concluido.");
    Serial.println();
}

void loop()
{
    const uint32_t agora = millis();

    // Subtracao antes da comparacao: imune ao estouro do millis()
    // aos 49 dias. O codigo do fabricante fazia
    // "millis() < inicio + periodo", que quebra no estouro.
    if (agora - ultimoPisca < INTERVALO_MS) {
        return;
    }

    ultimoPisca = agora;
    contador++;

    digitalWrite(LED_PIN, contador % 2);

    Serial.printf("[%4lu] uptime=%lus  heap_livre=%lu bytes\n",
                  (unsigned long) contador,
                  (unsigned long) (agora / 1000),
                  (unsigned long) ESP.getFreeHeap());
}
