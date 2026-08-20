#pragma once

#include <cstdint>

#ifdef ARDUINO
#include <esp_attr.h>
#endif

// Trocar aqui quando o dupont confirmar pulsos/volta
// (specs/pendencias-hardware.md #1). Ver a nota de sinal em
// periodoParaVelocidadeMs sobre por que multiplicar, nao dividir.
constexpr float PULSOS_POR_VOLTA = 1.0f;

// v(m/s) = 1319 / (T(ms) * PULSOS_POR_VOLTA).
//
// Por que multiplicar T por PULSOS_POR_VOLTA, nao dividir o resultado:
// com 2 pulsos/volta, o periodo OBSERVADO entre pulsos e a METADE do
// periodo de uma rotacao completa (2 eventos por volta, nao 1) -- entao
// o periodo real de rotacao e T_observado * PULSOS_POR_VOLTA. Aplicar a
// constante sobre esse periodo corrigido da a velocidade certa direto.
float periodoParaVelocidadeMs(uint32_t periodoMicros);

// Grava um timestamp no buffer se houver espaco. Pura: nao mexe em
// estado global, nao aloca -- por isso e testavel nativamente mesmo
// sendo chamada de dentro de uma ISR (anemometro.cpp). Quem chama
// decide o que fazer quando devolve false (normalmente, incrementar
// um contador de descarte).
//
// IRAM_ATTR no build do ESP32: esta funcao e chamada de dentro de
// isrPulso() (IRAM_ATTR, anemometro.cpp). Sem IRAM_ATTR aqui tambem,
// o codigo residiria em flash -- hoje isso so mascara a interrupcao
// durante escritas de flash (perda silenciosa de pulsos); se
// CONFIG_ARDUINO_ISR_IRAM for ligado no futuro, viraria crash (ISR
// pulando pra flash com o cache desligado). Ver specs/pendencias-hardware.md.
// No build nativo (sem ARDUINO) nao existe IRAM_ATTR nem faz sentido.
#ifdef ARDUINO
IRAM_ATTR bool gravarTimestampSeCouber(uint32_t* buffer, uint32_t capacidade,
                                        uint32_t totalAtual, uint32_t novoTimestamp);
#else
bool gravarTimestampSeCouber(uint32_t* buffer, uint32_t capacidade,
                              uint32_t totalAtual, uint32_t novoTimestamp);
#endif

struct JanelaDePulsos {
    uint32_t contagem;                 // pulsos desde a ultima leitura -- SEMPRE
                                        // exato, independe do buffer (alimenta a media)
    uint32_t ultimoPeriodoMicros;       // periodo entre os 2 ultimos pulsos
    uint32_t microsDesdeUltimoPulso;    // para o timeout de calmaria
    const uint32_t* timestamps;         // timestamps (micros) dos pulsos da janela
    uint32_t totalTimestamps;           // quantos cabem no buffer (cap 320)
    uint32_t descartadosPorBuffer;      // pulsos alem do buffer -- so degrada a
                                        // resolucao da rajada, nunca a media
};

struct Amostra {
    float avgSpeedMs;
    float gustSpeedMs;
};

// Unica fronteira entre o modulo de hardware e o de matematica: recebe a
// janela crua, devolve o par pronto pro payload da Fase 5.
//
// `duracaoJanelaMs` e a duracao REAL da janela (main.cpp mede via
// millis() antes de zerar o timer) -- NAO assume 10000 fixo. A Fase 5
// pode bloquear o loop() por mais que 10s durante um envio HTTP lento;
// se a media dividisse por 10 fixo mesmo com uma janela mais longa, o
// valor reportado sairia inflado (achado da revisao final da Fase 5,
// ver specs/pendencias-hardware.md #6). A rajada NAO precisa desse
// parametro -- o algoritmo dela ja opera sobre os timestamps brutos
// numa janela deslizante de 3s, sem assumir duracao total nenhuma.
Amostra calcularAmostra(const JanelaDePulsos& janela, uint32_t duracaoJanelaMs);

// So para uso local (Serial), NAO vai no payload da Fase 5 -- o backend
// so aceita avg_speed_ms/gust_speed_ms por janela de 10s, nao um valor
// "agora". Aplica o timeout de calmaria: retorna 0 se
// microsDesdeUltimoPulso > 10s, mesmo que ultimoPeriodoMicros implique
// velocidade diferente de zero.
float velocidadeInstantaneaMs(const JanelaDePulsos& janela);
