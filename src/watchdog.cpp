#include "watchdog.h"

#include <esp_task_wdt.h>

namespace {
// Folga confortavel sobre o pior caso realista de uma unica volta do
// loop(): um POST com timeout de 8s (ver envio.cpp), mais o tempo de
// uma reconexao de Wi-Fi (nao-bloqueante, mas pode levar alguns ciclos).
// O watchdog e alimentado entre cada lote de um esvaziamento com varios
// lotes (ver Task 6), entao o timeout aqui protege contra loop() travado
// de verdade, nao contra um envio HTTP legitimamente lento.
constexpr uint32_t TIMEOUT_SEGUNDOS = 30;
}  // namespace

void iniciarWatchdog()
{
    esp_task_wdt_init(TIMEOUT_SEGUNDOS, true);  // true = reinicia o ESP32 no timeout
    esp_task_wdt_add(NULL);  // registra a task atual (loop() do Arduino)
}

void alimentarWatchdog()
{
    esp_task_wdt_reset();
}
