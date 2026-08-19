#include "medicao.h"

float periodoParaVelocidadeMs(uint32_t periodoMicros)
{
    if (periodoMicros == 0) {
        return 0.0f;
    }

    const float periodoMs = periodoMicros / 1000.0f;
    return 1319.0f / (periodoMs * PULSOS_POR_VOLTA);
}
