#pragma once

// Task Watchdog Timer nativo do ESP32. Se o loop() nao alimentar o
// watchdog dentro do timeout, o dispositivo reinicia sozinho -- melhor
// um reboot visivel (aparece nos logs de uptime) do que ficar travado
// no topo da caixa d'agua sem ninguem notar.
//
// Chame iniciarWatchdog() uma vez no setup(). Chame alimentarWatchdog()
// a cada volta do loop() -- e tambem entre cada lote HTTP de um
// esvaziamento de telemetria com varios lotes seguidos, para um
// esvaziamento demorado nao disparar um reset por conta propria.
void iniciarWatchdog();
void alimentarWatchdog();
