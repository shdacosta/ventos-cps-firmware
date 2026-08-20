#include "envio.h"

#include <Arduino.h>
#include <HTTPClient.h>

#include "secrets.h"
#include "telemetria.h"
#include "watchdog.h"
#include "wifi_gerenciado.h"

namespace {

constexpr const char* DEVICE_ID        = "anemometro-01";
constexpr const char* FIRMWARE_VERSION = "1.0.0";
constexpr uint32_t    TIMEOUT_HTTP_MS  = 8000;

BufferTelemetria bufferTelemetria;
uint32_t         ultimoDescartadasReportado = 0;

// Alocados em heap por iniciarEnvio(), nao static/.bss -- ver comentario
// em envio.h. Continuam validos para o resto da vida do programa (nunca
// sao liberados), so nao ocupam a regiao estatica apertada do linker.
char*              payloadJson = nullptr;
AmostraTelemetria* loteSaida   = nullptr;

}  // namespace

void iniciarEnvio()
{
    payloadJson = new char[CAPACIDADE_PAYLOAD_JSON];
    loteSaida   = new AmostraTelemetria[MAX_AMOSTRAS_POR_LOTE];
}

void registrarAmostra(const Amostra& amostra)
{
    if (!relogioSincronizado()) {
        Serial.println("[telemetria] relogio ainda nao sincronizado, amostra nao guardada");
        return;
    }

    const AmostraTelemetria registro = {
        (uint32_t) horaAtualUnix(), amostra.avgSpeedMs, amostra.gustSpeedMs
    };
    inserirAmostra(bufferTelemetria, registro);
}

void tentarEnviarLotes()
{
    // Antes da checagem de Wi-Fi de proposito: o overflow do buffer so
    // acontece depois de ~4h de Wi-Fi caido (buffer cheio, sem
    // conseguir esvaziar) -- exatamente o cenario em que wifiConectado()
    // e false. Se este bloco ficasse depois do return abaixo, o caso
    // mais importante de avisar sobre overflow seria justamente o caso
    // em que o aviso nunca apareceria no Serial.
    if (bufferTelemetria.descartadasPorOverflow != ultimoDescartadasReportado) {
        Serial.printf("[telemetria] descartadas_por_overflow=%lu (total desde o boot)\n",
                      (unsigned long) bufferTelemetria.descartadasPorOverflow);
        ultimoDescartadasReportado = bufferTelemetria.descartadasPorOverflow;
    }

    if (!wifiConectado()) {
        return;
    }

    uint32_t lotesEnviados = 0;
    while (bufferTelemetria.quantidade > 0 && lotesEnviados < MAX_LOTES_POR_CICLO) {
        const uint32_t n = copiarProximoLote(bufferTelemetria, loteSaida, MAX_AMOSTRAS_POR_LOTE);

        const size_t escrito = montarPayloadJson(
            loteSaida, n, DEVICE_ID, FIRMWARE_VERSION,
            (uint32_t) (millis() / 1000), ESP.getFreeHeap(), wifiRssiDbm(),
            wifiContagemReconexoes(),
            payloadJson, CAPACIDADE_PAYLOAD_JSON);

        if (escrito == 0) {
            // Nao deveria acontecer com CAPACIDADE_PAYLOAD_JSON
            // dimensionado corretamente (ver teste de pior caso em
            // test_telemetria.cpp) -- mas se acontecer, falha alto em
            // vez de mandar payload truncado.
            Serial.println("[telemetria] ERRO: payload nao coube no buffer, lote NAO enviado");
            break;
        }

        HTTPClient http;
        http.begin(INGEST_URL);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", String("Bearer ") + INGEST_TOKEN);
        http.setTimeout(TIMEOUT_HTTP_MS);
        http.setConnectTimeout(TIMEOUT_HTTP_MS);

        const int codigo = http.POST((uint8_t*) payloadJson, escrito);
        http.end();

        // Entre cada lote -- um esvaziamento de varios lotes seguidos
        // nao pode disparar o watchdog por demorar mais que o timeout
        // dele.
        alimentarWatchdog();

        const AcaoAposResposta acao = decidirAcaoAposResposta(codigo);

        if (acao == AcaoAposResposta::Manter) {
            Serial.printf("[telemetria] envio falhou (codigo=%d), mantendo %lu amostras no buffer\n",
                          codigo, (unsigned long) n);
            break;
        }

        if (acao == AcaoAposResposta::Descartar) {
            Serial.printf("[telemetria] lote rejeitado (codigo=%d), descartando %lu amostras\n",
                          codigo, (unsigned long) n);
        }

        removerMaisAntigas(bufferTelemetria, n);
        lotesEnviados++;
    }
}
