#pragma once

#include "medicao.h"

// Configura o GPIO e anexa a interrupcao. Chamar uma vez no setup().
void iniciarAnemometro();

// Copia atomica do estado acumulado E ZERA os acumuladores. Cada
// chamada fecha uma janela -- por isso deve ser chamada a cada 10s,
// nunca em outro ritmo (e o contrato que o backend espera).
JanelaDePulsos lerEZerarJanela();
