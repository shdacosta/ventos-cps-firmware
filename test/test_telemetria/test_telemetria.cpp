#include <unity.h>

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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_inserir_com_espaco_sobrando);
    RUN_TEST(test_inserir_buffer_cheio_sobrescreve_mais_antiga);
    RUN_TEST(test_copiar_lote_menor_que_capacidade_pedida);
    RUN_TEST(test_copiar_lote_capado_no_maximo_pedido);
    RUN_TEST(test_remover_mais_antigas_depois_inserir_nao_corrompe_indices);
    RUN_TEST(test_remover_mais_que_quantidade_disponivel_nao_estoura);
    RUN_TEST(test_buffer_vazio_copiar_e_remover_nao_fazem_nada);
    return UNITY_END();
}
