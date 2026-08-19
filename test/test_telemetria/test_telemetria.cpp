#include <unity.h>

#include <ArduinoJson.h>

#include "telemetria.h"

void test_inserir_com_espaco_sobrando(void) {
    BufferTelemetria buffer = {};
    AmostraTelemetria amostra = {1000, 3.5f, 5.0f};

    inserirAmostra(buffer, amostra);

    TEST_ASSERT_EQUAL_UINT32(1, buffer.quantidade);
    TEST_ASSERT_EQUAL_UINT32(0, buffer.inicio);
    TEST_ASSERT_EQUAL_UINT32(1000, buffer.amostras[0].measuredAt);
}

void test_inserir_buffer_cheio_sobrescreve_mais_antiga(void) {
    BufferTelemetria buffer = {};
    for (uint32_t i = 0; i < CAPACIDADE_BUFFER_TELEMETRIA; i++) {
        AmostraTelemetria a = {i, (float) i, (float) i};
        inserirAmostra(buffer, a);
    }
    TEST_ASSERT_EQUAL_UINT32(CAPACIDADE_BUFFER_TELEMETRIA, buffer.quantidade);
    TEST_ASSERT_EQUAL_UINT32(0, buffer.descartadasPorOverflow);

    AmostraTelemetria nova = {99999, 7.0f, 9.0f};
    inserirAmostra(buffer, nova);

    TEST_ASSERT_EQUAL_UINT32(CAPACIDADE_BUFFER_TELEMETRIA, buffer.quantidade);
    TEST_ASSERT_EQUAL_UINT32(1, buffer.descartadasPorOverflow);

    // A mais antiga que sobra agora deve ser measuredAt=1 (a original
    // measuredAt=0 foi descartada para abrir espaco para a nova)
    AmostraTelemetria saida[1];
    copiarProximoLote(buffer, saida, 1);
    TEST_ASSERT_EQUAL_UINT32(1, saida[0].measuredAt);
}

void test_copiar_lote_menor_que_capacidade_pedida(void) {
    BufferTelemetria buffer = {};
    inserirAmostra(buffer, AmostraTelemetria{100, 1.0f, 2.0f});
    inserirAmostra(buffer, AmostraTelemetria{110, 1.5f, 2.5f});

    AmostraTelemetria saida[10];
    uint32_t copiadas = copiarProximoLote(buffer, saida, 10);

    TEST_ASSERT_EQUAL_UINT32(2, copiadas);
    TEST_ASSERT_EQUAL_UINT32(100, saida[0].measuredAt);
    TEST_ASSERT_EQUAL_UINT32(110, saida[1].measuredAt);
}

void test_copiar_lote_capado_no_maximo_pedido(void) {
    BufferTelemetria buffer = {};
    for (uint32_t i = 0; i < 5; i++) {
        inserirAmostra(buffer, AmostraTelemetria{100 + i, (float) i, (float) i});
    }

    AmostraTelemetria saida[3];
    uint32_t copiadas = copiarProximoLote(buffer, saida, 3);

    TEST_ASSERT_EQUAL_UINT32(3, copiadas);
    TEST_ASSERT_EQUAL_UINT32(100, saida[0].measuredAt);
    TEST_ASSERT_EQUAL_UINT32(102, saida[2].measuredAt);
    // copiar nao remove -- buffer inalterado
    TEST_ASSERT_EQUAL_UINT32(5, buffer.quantidade);
}

void test_remover_mais_antigas_depois_inserir_nao_corrompe_indices(void) {
    BufferTelemetria buffer = {};
    for (uint32_t i = 0; i < CAPACIDADE_BUFFER_TELEMETRIA; i++) {
        inserirAmostra(buffer, AmostraTelemetria{i, (float) i, (float) i});
    }

    removerMaisAntigas(buffer, CAPACIDADE_BUFFER_TELEMETRIA / 2);
    TEST_ASSERT_EQUAL_UINT32(CAPACIDADE_BUFFER_TELEMETRIA / 2, buffer.quantidade);

    for (uint32_t i = 0; i < 10; i++) {
        inserirAmostra(buffer, AmostraTelemetria{90000 + i, (float) i, (float) i});
    }
    TEST_ASSERT_EQUAL_UINT32(CAPACIDADE_BUFFER_TELEMETRIA / 2 + 10, buffer.quantidade);

    // A mais antiga que sobra deve ser measuredAt = CAPACIDADE/2 (a
    // primeira metade original foi removida)
    AmostraTelemetria saida[1];
    copiarProximoLote(buffer, saida, 1);
    TEST_ASSERT_EQUAL_UINT32(CAPACIDADE_BUFFER_TELEMETRIA / 2, saida[0].measuredAt);
}

void test_remover_mais_que_quantidade_disponivel_nao_estoura(void) {
    BufferTelemetria buffer = {};
    inserirAmostra(buffer, AmostraTelemetria{1, 1.0f, 1.0f});
    inserirAmostra(buffer, AmostraTelemetria{2, 2.0f, 2.0f});

    removerMaisAntigas(buffer, 100);

    TEST_ASSERT_EQUAL_UINT32(0, buffer.quantidade);
}

void test_buffer_vazio_copiar_e_remover_nao_fazem_nada(void) {
    BufferTelemetria buffer = {};

    // Buffer recém-criado (estado no primeiro boot)
    TEST_ASSERT_EQUAL_UINT32(0, buffer.quantidade);
    TEST_ASSERT_EQUAL_UINT32(0, buffer.inicio);

    // Copiar de um buffer vazio deve retornar 0
    AmostraTelemetria saida[10];
    uint32_t copiadas = copiarProximoLote(buffer, saida, 10);
    TEST_ASSERT_EQUAL_UINT32(0, copiadas);

    // Remover de um buffer vazio não deve corromper nada
    removerMaisAntigas(buffer, 5);
    TEST_ASSERT_EQUAL_UINT32(0, buffer.quantidade);
    TEST_ASSERT_EQUAL_UINT32(0, buffer.inicio);
}

void test_acao_2xx_remove(void) {
    TEST_ASSERT_TRUE(AcaoAposResposta::Remover == decidirAcaoAposResposta(200));
    TEST_ASSERT_TRUE(AcaoAposResposta::Remover == decidirAcaoAposResposta(201));
}

void test_acao_4xx_descarta(void) {
    TEST_ASSERT_TRUE(AcaoAposResposta::Descartar == decidirAcaoAposResposta(400));
    TEST_ASSERT_TRUE(AcaoAposResposta::Descartar == decidirAcaoAposResposta(422));
}

void test_acao_401_e_403_mantem(void) {
    TEST_ASSERT_TRUE(AcaoAposResposta::Manter == decidirAcaoAposResposta(401));
    TEST_ASSERT_TRUE(AcaoAposResposta::Manter == decidirAcaoAposResposta(403));
}

void test_acao_5xx_mantem(void) {
    TEST_ASSERT_TRUE(AcaoAposResposta::Manter == decidirAcaoAposResposta(500));
    TEST_ASSERT_TRUE(AcaoAposResposta::Manter == decidirAcaoAposResposta(503));
}

void test_acao_erro_de_conexao_mantem(void) {
    // HTTPClient do Arduino devolve codigo negativo quando nao ha
    // resposta HTTP de verdade (timeout, recusa de conexao, etc.)
    TEST_ASSERT_TRUE(AcaoAposResposta::Manter == decidirAcaoAposResposta(-1));
    TEST_ASSERT_TRUE(AcaoAposResposta::Manter == decidirAcaoAposResposta(-11));
}

void test_montar_payload_estrutura_correta(void) {
    AmostraTelemetria amostras[2] = {
        {1755432000, 3.21f, 5.84f},
        {1755432010, 3.44f, 4.10f},
    };

    char saida[512];
    size_t escrito = montarPayloadJson(amostras, 2, "anemometro-01", "1.0.0",
                                        86400, 351476, -62, saida, sizeof(saida));

    TEST_ASSERT_TRUE(escrito > 0);

    JsonDocument doc;
    DeserializationError erro = deserializeJson(doc, saida, escrito);
    TEST_ASSERT_TRUE(erro == DeserializationError::Ok);

    TEST_ASSERT_EQUAL_STRING("anemometro-01", doc["device_id"]);
    TEST_ASSERT_EQUAL_STRING("1.0.0", doc["firmware_version"]);
    TEST_ASSERT_EQUAL_UINT32(86400, doc["health"]["uptime_seconds"]);
    TEST_ASSERT_EQUAL_UINT32(351476, doc["health"]["free_heap_bytes"]);
    TEST_ASSERT_EQUAL_INT(-62, doc["health"]["wifi_rssi_dbm"]);

    TEST_ASSERT_EQUAL_UINT32(2, doc["samples"].size());
    TEST_ASSERT_EQUAL_UINT32(1755432000, doc["samples"][0]["measured_at"]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.21f, doc["samples"][0]["avg_speed_ms"]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.84f, doc["samples"][0]["gust_speed_ms"]);
    TEST_ASSERT_EQUAL_UINT32(1755432010, doc["samples"][1]["measured_at"]);
}

void test_montar_payload_buffer_pequeno_demais_devolve_zero(void) {
    AmostraTelemetria amostras[1] = {{1000, 1.0f, 2.0f}};

    char saidaMinuscula[5];  // com certeza pequeno demais
    size_t escrito = montarPayloadJson(amostras, 1, "anemometro-01", "1.0.0",
                                        100, 200000, -50, saidaMinuscula, sizeof(saidaMinuscula));

    TEST_ASSERT_EQUAL_UINT32(0, escrito);
}

void test_montar_payload_500_amostras_pior_caso_cabe_no_buffer(void) {
    // Pior caso de tamanho: 500 amostras (o maximo por lote) com o maior
    // numero de digitos plausivel (timestamp de 10 digitos, velocidades
    // negativas com sinal -- nunca acontece de verdade, mas testa a
    // string mais longa que o formato permite). Prova que
    // CAPACIDADE_PAYLOAD_JSON realmente comporta o pior caso, em vez de
    // confiar numa conta feita a mao.
    static AmostraTelemetria amostras[MAX_AMOSTRAS_POR_LOTE];
    for (uint32_t i = 0; i < MAX_AMOSTRAS_POR_LOTE; i++) {
        amostras[i] = AmostraTelemetria{2000000000u + i, -12.345678f, -37.500000f};
    }

    static char saida[CAPACIDADE_PAYLOAD_JSON];
    size_t escrito = montarPayloadJson(amostras, MAX_AMOSTRAS_POR_LOTE,
                                        "anemometro-01", "1.0.0",
                                        999999999, 999999999, -100,
                                        saida, sizeof(saida));

    TEST_ASSERT_TRUE(escrito > 0);
    TEST_ASSERT_TRUE(escrito < CAPACIDADE_PAYLOAD_JSON);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_inserir_com_espaco_sobrando);
    RUN_TEST(test_inserir_buffer_cheio_sobrescreve_mais_antiga);
    RUN_TEST(test_copiar_lote_menor_que_capacidade_pedida);
    RUN_TEST(test_copiar_lote_capado_no_maximo_pedido);
    RUN_TEST(test_remover_mais_antigas_depois_inserir_nao_corrompe_indices);
    RUN_TEST(test_remover_mais_que_quantidade_disponivel_nao_estoura);
    RUN_TEST(test_buffer_vazio_copiar_e_remover_nao_fazem_nada);
    RUN_TEST(test_acao_2xx_remove);
    RUN_TEST(test_acao_4xx_descarta);
    RUN_TEST(test_acao_401_e_403_mantem);
    RUN_TEST(test_acao_5xx_mantem);
    RUN_TEST(test_acao_erro_de_conexao_mantem);
    RUN_TEST(test_montar_payload_estrutura_correta);
    RUN_TEST(test_montar_payload_buffer_pequeno_demais_devolve_zero);
    RUN_TEST(test_montar_payload_500_amostras_pior_caso_cabe_no_buffer);
    return UNITY_END();
}
