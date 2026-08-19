#pragma once

#include "medicao.h"

// Configura o GPIO e anexa a interrupcao. Chamar uma vez no setup().
void iniciarAnemometro();

// Copia atomica do estado acumulado E ZERA os acumuladores. Cada
// chamada fecha uma janela -- por isso deve ser chamada a cada 10s,
// nunca em outro ritmo (e o contrato que o backend espera).
//
// Contrato do ponteiro: JanelaDePulsos::timestamps aponta para um
// buffer interno que so e valido ATE A PROXIMA CHAMADA desta funcao --
// a proxima chamada sobrescreve o mesmo buffer. Quem precisar reter os
// timestamps por mais tempo do que isso deve copiar o array, nunca
// guardar o ponteiro.
JanelaDePulsos lerEZerarJanela();
