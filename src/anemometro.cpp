#include "anemometro.h"

#include <Arduino.h>
#include <cstring>

namespace {

constexpr uint8_t  PINO_SENSOR       = 25;
constexpr uint32_t DEBOUNCE_MICROS   = 5000;  // 5ms
constexpr uint32_t CAPACIDADE_BUFFER = 320;

// Todo o estado tocado pela ISR precisa ser volatile: o compilador nao
// pode assumir que o loop() ve o mesmo valor que a ISR escreveu.
volatile uint32_t bufferTimestamps[CAPACIDADE_BUFFER];
volatile uint32_t totalTimestamps    = 0;
volatile uint32_t descartadosBuffer  = 0;
volatile uint32_t contagemTotal      = 0;
volatile uint32_t ultimoPulsoAceito  = 0;
volatile uint32_t penultimoPulsoAceito = 0;
volatile bool      houvePulso        = false;

// Snapshot nao-volatile do buffer, copiado atomicamente dentro de
// lerEZerarJanela(). Evita race: ISR nao escreve aqui, so em bufferTimestamps.
// Sem isso, caller (Task 8, calcularAmostra) poderia ler o array enquanto
// ISR escreve em bufferTimestamps logo apos a seção crítica terminar.
static uint32_t timestampsSnapshot[CAPACIDADE_BUFFER];

void IRAM_ATTR isrPulso()
{
    const uint32_t agora = micros();

    // Debounce: descarta qualquer transicao dentro de 5ms do ultimo
    // pulso ACEITO (nao do ultimo evento bruto).
    if (houvePulso && (agora - ultimoPulsoAceito) < DEBOUNCE_MICROS) {
        return;
    }

    if (houvePulso) {
        penultimoPulsoAceito = ultimoPulsoAceito;
    } else {
        // Primeiro pulso desde o boot: nao existe "penultimo" de
        // verdade. Usar o mesmo instante zera o periodo calculado --
        // periodoParaVelocidadeMs ja trata periodo=0 como 0 m/s. Sem
        // isso, o periodo seria "tempo desde o boot ate agora", um
        // numero sem nenhum sentido fisico.
        penultimoPulsoAceito = agora;
    }
    ultimoPulsoAceito = agora;
    houvePulso = true;

    // gravarTimestampSeCouber e pura (medicao.cpp) -- nao aloca, nao
    // bloqueia, seguro dentro de ISR mesmo operando sobre o buffer
    // volatile via cast.
    const bool gravou = gravarTimestampSeCouber(
        const_cast<uint32_t*>(bufferTimestamps), CAPACIDADE_BUFFER,
        totalTimestamps, agora);

    if (gravou) {
        totalTimestamps++;
    } else {
        descartadosBuffer++;
    }

    contagemTotal++;
}

}  // namespace

void iniciarAnemometro()
{
    pinMode(PINO_SENSOR, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PINO_SENSOR), isrPulso, FALLING);
}

JanelaDePulsos lerEZerarJanela()
{
    JanelaDePulsos janela = {};

    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);

    janela.contagem = contagemTotal;
    janela.ultimoPeriodoMicros = houvePulso
        ? (ultimoPulsoAceito - penultimoPulsoAceito)
        : 0;
    janela.microsDesdeUltimoPulso = houvePulso
        ? (micros() - ultimoPulsoAceito)
        : UINT32_MAX;  // nunca houve pulso -- calmaria total
    janela.totalTimestamps = totalTimestamps;
    janela.descartadosPorBuffer = descartadosBuffer;

    // Copiar timestamps ANTES de zerar, ainda dentro da seção crítica.
    // Isso evita race: memcpy cria snapshot atomicamente, ISR nao pode
    // escrever em timestampsSnapshot enquanto estamos aqui.
    memcpy(timestampsSnapshot, const_cast<const uint32_t*>(bufferTimestamps),
           janela.totalTimestamps * sizeof(uint32_t));

    contagemTotal = 0;
    totalTimestamps = 0;
    descartadosBuffer = 0;

    portEXIT_CRITICAL(&mux);

    // timestamps aponta pro snapshot nao-volatile, que nao sera escrito
    // novamente. Snapshot e imutavel apos a seção crítica acima.
    janela.timestamps = timestampsSnapshot;

    return janela;
}
