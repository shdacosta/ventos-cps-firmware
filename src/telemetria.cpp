#include "telemetria.h"

void inserirAmostra(BufferTelemetria& buffer, const AmostraTelemetria& amostra)
{
    if (buffer.quantidade < CAPACIDADE_BUFFER_TELEMETRIA) {
        const uint32_t posicao = (buffer.inicio + buffer.quantidade) % CAPACIDADE_BUFFER_TELEMETRIA;
        buffer.amostras[posicao] = amostra;
        buffer.quantidade++;
        return;
    }

    // Buffer cheio: sobrescreve a mais antiga (buffer.inicio) e avanca o
    // inicio -- preserva o mais recente, descarta o mais antigo.
    buffer.amostras[buffer.inicio] = amostra;
    buffer.inicio = (buffer.inicio + 1) % CAPACIDADE_BUFFER_TELEMETRIA;
    buffer.descartadasPorOverflow++;
}

uint32_t copiarProximoLote(const BufferTelemetria& buffer,
                            AmostraTelemetria* saida, uint32_t capacidadeSaida)
{
    const uint32_t n = buffer.quantidade < capacidadeSaida ? buffer.quantidade : capacidadeSaida;

    for (uint32_t i = 0; i < n; i++) {
        const uint32_t posicao = (buffer.inicio + i) % CAPACIDADE_BUFFER_TELEMETRIA;
        saida[i] = buffer.amostras[posicao];
    }

    return n;
}

void removerMaisAntigas(BufferTelemetria& buffer, uint32_t n)
{
    const uint32_t remover = n < buffer.quantidade ? n : buffer.quantidade;
    buffer.inicio = (buffer.inicio + remover) % CAPACIDADE_BUFFER_TELEMETRIA;
    buffer.quantidade -= remover;
}
