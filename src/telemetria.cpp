#include "telemetria.h"

#include <ArduinoJson.h>

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

AcaoAposResposta decidirAcaoAposResposta(int codigo)
{
    if (codigo < 0) {
        return AcaoAposResposta::Manter;
    }
    if (codigo >= 200 && codigo < 300) {
        return AcaoAposResposta::Remover;
    }
    if (codigo >= 400 && codigo < 500) {
        return AcaoAposResposta::Descartar;
    }
    return AcaoAposResposta::Manter;
}

size_t montarPayloadJson(const AmostraTelemetria* amostras, uint32_t total,
                          const char* deviceId, const char* firmwareVersion,
                          uint32_t uptimeSeconds, uint32_t freeHeapBytes,
                          int wifiRssiDbm,
                          char* saida, size_t capacidadeSaida)
{
    JsonDocument doc;

    doc["device_id"] = deviceId;
    doc["firmware_version"] = firmwareVersion;

    JsonObject health = doc["health"].to<JsonObject>();
    health["uptime_seconds"] = uptimeSeconds;
    health["free_heap_bytes"] = freeHeapBytes;
    health["wifi_rssi_dbm"] = wifiRssiDbm;

    JsonArray samples = doc["samples"].to<JsonArray>();
    for (uint32_t i = 0; i < total; i++) {
        JsonObject amostra = samples.add<JsonObject>();
        amostra["measured_at"] = amostras[i].measuredAt;
        amostra["avg_speed_ms"] = amostras[i].avgSpeedMs;
        amostra["gust_speed_ms"] = amostras[i].gustSpeedMs;
    }

    const size_t escrito = serializeJson(doc, saida, capacidadeSaida);
    if (escrito == 0 || escrito >= capacidadeSaida) {
        return 0;
    }
    return escrito;
}
