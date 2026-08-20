#include "medicao.h"

namespace {

constexpr uint32_t JANELA_RAJADA_MICROS = 3000000;  // 3s
constexpr uint32_t TIMEOUT_CALMARIA_MICROS = 10000000;  // 10s

float calcularPicoDeRajada(const uint32_t* timestamps, uint32_t total)
{
    if (total < 2) {
        return 0.0f;  // precisa de pelo menos 2 pontos pra ter um periodo
    }

    float picoHz = 0.0f;
    uint32_t inicio = 0;

    // Dois ponteiros: para cada "fim" avancando, retrocede "inicio" ate
    // caber na janela de 3s. Pulsos dentro de [fim - 3s, fim] contam.
    for (uint32_t fim = 0; fim < total; fim++) {
        while (timestamps[fim] - timestamps[inicio] > JANELA_RAJADA_MICROS) {
            inicio++;
        }

        // PERIODOS entre pulsos, nao pontos: N pontos uniformemente
        // espacados cobrem N-1 periodos. Contar pontos superestimaria
        // a frequencia sistematicamente -- 10 pulsos a 1s dariam "rajada"
        // de 1,33 Hz contra uma media real de 1,0 Hz, quebrando a
        // invariante "vento constante = rajada igual a media".
        const uint32_t periodosNaJanela = fim - inicio;
        if (periodosNaJanela == 0) {
            continue;  // um unico ponto na janela, sem periodo formado
        }

        const float freqHz = periodosNaJanela / 3.0f;
        if (freqHz > picoHz) {
            picoHz = freqHz;
        }
    }

    return picoHz;
}

}  // namespace

float periodoParaVelocidadeMs(uint32_t periodoMicros)
{
    if (periodoMicros == 0) {
        return 0.0f;
    }

    const float periodoMs = periodoMicros / 1000.0f;
    return 1319.0f / (periodoMs * PULSOS_POR_VOLTA);
}

#ifdef ARDUINO
IRAM_ATTR bool gravarTimestampSeCouber(uint32_t* buffer, uint32_t capacidade,
                                        uint32_t totalAtual, uint32_t novoTimestamp)
#else
bool gravarTimestampSeCouber(uint32_t* buffer, uint32_t capacidade,
                              uint32_t totalAtual, uint32_t novoTimestamp)
#endif
{
    if (totalAtual >= capacidade) {
        return false;
    }

    buffer[totalAtual] = novoTimestamp;
    return true;
}

Amostra calcularAmostra(const JanelaDePulsos& janela, uint32_t duracaoJanelaMs)
{
    Amostra amostra = {};

    if (duracaoJanelaMs == 0) {
        return amostra;  // media/rajada zero -- nunca deveria acontecer no
                          // caminho real, mas evita divisao por zero
    }

    const float freqMediaHz = janela.contagem / (duracaoJanelaMs / 1000.0f);
    amostra.avgSpeedMs = 1.319f * freqMediaHz / PULSOS_POR_VOLTA;

    const float picoHz = calcularPicoDeRajada(janela.timestamps, janela.totalTimestamps);
    amostra.gustSpeedMs = 1.319f * picoHz / PULSOS_POR_VOLTA;

    // A rajada crua tem um vies sistematico para baixo (denominador
    // fixo de 3s no calculo de frequencia, enquanto o numero de
    // periodos capturados e sempre um pouco menor que o teorico) --
    // esse clamp dispara ROTINEIRAMENTE em vento estavel, nao e so
    // defensivo. Nao remova achando que e codigo morto.
    if (amostra.gustSpeedMs < amostra.avgSpeedMs) {
        amostra.gustSpeedMs = amostra.avgSpeedMs;
    }

    return amostra;
}

float velocidadeInstantaneaMs(const JanelaDePulsos& janela)
{
    if (janela.microsDesdeUltimoPulso > TIMEOUT_CALMARIA_MICROS) {
        return 0.0f;
    }

    return periodoParaVelocidadeMs(janela.ultimoPeriodoMicros);
}
