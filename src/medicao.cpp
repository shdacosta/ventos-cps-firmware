#include "medicao.h"

float periodoParaVelocidadeMs(uint32_t periodoMicros)
{
    if (periodoMicros == 0) {
        return 0.0f;
    }

    const float periodoMs = periodoMicros / 1000.0f;
    return 1319.0f / (periodoMs * PULSOS_POR_VOLTA);
}

bool gravarTimestampSeCouber(uint32_t* buffer, uint32_t capacidade,
                              uint32_t totalAtual, uint32_t novoTimestamp)
{
    if (totalAtual >= capacidade) {
        return false;
    }

    buffer[totalAtual] = novoTimestamp;
    return true;
}

Amostra calcularAmostra(const JanelaDePulsos& janela)
{
    Amostra amostra;

    const float freqHz = janela.contagem / 10.0f;
    amostra.avgSpeedMs = 1.319f * freqHz / PULSOS_POR_VOLTA;

    amostra.gustSpeedMs = amostra.avgSpeedMs;  // placeholder ate a Task 5

    return amostra;
}
