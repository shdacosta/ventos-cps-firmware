#pragma once

#include <cstddef>
#include <cstdint>

// ~4h de buffer a 10s/amostra. So em RAM -- decisao deliberada, ver
// specs/telemetria.md #9: perda de dados nao-enviados em reinicio e
// aceitavel para esta estacao (ligada na tomada, reinicios devem ser
// raros); simplicidade e ausencia de desgaste de flash pesaram mais.
constexpr uint32_t CAPACIDADE_BUFFER_TELEMETRIA = 1440;

// Limite do backend por requisicao (backend/README.md).
constexpr uint32_t MAX_AMOSTRAS_POR_LOTE = 500;

// Teto de lotes seguidos numa unica tentativa de esvaziamento. Dado
// CAPACIDADE_BUFFER_TELEMETRIA=1440 e MAX_AMOSTRAS_POR_LOTE=500, 3 lotes
// (ceil(1440/500)) ja bastam pra esvaziar o buffer inteiro numa unica
// passada -- existe como teto explicito pra nao depender de recalcular
// esse numero a mao se a capacidade do buffer mudar no futuro.
constexpr uint32_t MAX_LOTES_POR_CICLO = 3;

struct AmostraTelemetria {
    uint32_t measuredAt;   // unix UTC, segundos -- so grava se o relogio
                            // (NTP) ja estiver sincronizado
    float    avgSpeedMs;
    float    gustSpeedMs;
};

struct BufferTelemetria {
    AmostraTelemetria amostras[CAPACIDADE_BUFFER_TELEMETRIA];
    uint32_t inicio;                  // indice circular da amostra mais antiga
    uint32_t quantidade;              // quantas amostras validas tem agora
    uint32_t descartadasPorOverflow;  // contador so-diagnostico (Serial) --
                                       // nao entra no payload
};

// Insere uma amostra. Se o buffer estiver cheio, sobrescreve a mais
// antiga (preserva o mais recente) e incrementa descartadasPorOverflow.
void inserirAmostra(BufferTelemetria& buffer, const AmostraTelemetria& amostra);

// Copia ate `capacidadeSaida` das amostras mais antigas para `saida`,
// SEM remover do buffer -- quem chama decide se remove, depois de saber
// o resultado do envio. Devolve quantas copiou (pode ser menos que
// capacidadeSaida se o buffer tiver menos amostras que isso).
uint32_t copiarProximoLote(const BufferTelemetria& buffer,
                            AmostraTelemetria* saida, uint32_t capacidadeSaida);

// Remove as `n` amostras mais antigas -- chamado depois de confirmar que
// foram aceitas (2xx) ou definitivamente rejeitadas (4xx). Se `n` for
// maior que a quantidade disponivel, remove so o que tem (nunca da
// numero negativo).
void removerMaisAntigas(BufferTelemetria& buffer, uint32_t n);

enum class AcaoAposResposta {
    Remover,    // 2xx -- lote aceito, remove do buffer
    Descartar,  // 4xx (exceto 401/403) -- lote invalido (payload
                // malformado, lote grande demais) -- reenviar o mesmo
                // lote so repetiria o mesmo erro, entao descarta e loga
    Manter,     // erro de conexao/timeout, 401/403 (token errado ou
                // rotacionado -- recuperavel, o mesmo lote passa a ser
                // aceito assim que o token for corrigido), ou 5xx --
                // problema transitorio, mantem no buffer para a proxima
                // tentativa
};

// `codigo` segue a convencao do HTTPClient do Arduino: negativo = erro
// de conexao/timeout (sem resposta HTTP de verdade), positivo = status
// HTTP de verdade.
AcaoAposResposta decidirAcaoAposResposta(int codigo);

// Pior caso: 500 amostras (MAX_AMOSTRAS_POR_LOTE) de ate ~82 bytes cada
// em JSON (~41KB) + cabecalho (device_id, firmware_version, health,
// ~250 bytes) -- com folga. O teste
// test_montar_payload_500_amostras_pior_caso_cabe_no_buffer prova isso
// de verdade, em vez de confiar só nesta conta.
constexpr size_t CAPACIDADE_PAYLOAD_JSON = 49152;

// Monta o corpo JSON de um lote (ate MAX_AMOSTRAS_POR_LOTE amostras) no
// formato exato que o backend espera (backend/README.md). Escreve em
// `saida` (capacidade `capacidadeSaida`), devolve os bytes escritos, ou
// 0 se nao coube -- chamador deve tratar como erro (nao deveria
// acontecer com CAPACIDADE_PAYLOAD_JSON, ver teste de pior caso).
size_t montarPayloadJson(const AmostraTelemetria* amostras, uint32_t total,
                          const char* deviceId, const char* firmwareVersion,
                          uint32_t uptimeSeconds, uint32_t freeHeapBytes,
                          int wifiRssiDbm, uint32_t wifiReconnectCount,
                          char* saida, size_t capacidadeSaida);
