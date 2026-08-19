#include <unity.h>

#include "medicao.h"

void test_periodo_para_velocidade_partida(void) {
    // 0,7 km/h -> periodo de 6,8s = 6800000 micros (tabela de hardware.md)
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.194f, periodoParaVelocidadeMs(6800000));
}

void test_periodo_para_velocidade_maxima(void) {
    // 135 km/h -> periodo de 35ms = 35000 micros
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 37.5f, periodoParaVelocidadeMs(35000));
}

void test_periodo_para_velocidade_10kmh(void) {
    // 10 km/h -> periodo de 475ms = 475000 micros
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 2.78f, periodoParaVelocidadeMs(475000));
}

void test_gravar_com_espaco_sobra(void) {
    uint32_t buffer[3] = {0, 0, 0};

    bool gravou = gravarTimestampSeCouber(buffer, 3, 0, 111);

    TEST_ASSERT_TRUE(gravou);
    TEST_ASSERT_EQUAL_UINT32(111, buffer[0]);
}

void test_gravar_na_ultima_posicao_livre(void) {
    uint32_t buffer[3] = {10, 20, 0};

    bool gravou = gravarTimestampSeCouber(buffer, 3, 2, 30);

    TEST_ASSERT_TRUE(gravou);
    TEST_ASSERT_EQUAL_UINT32(30, buffer[2]);
}

void test_gravar_buffer_cheio_nao_escreve_fora(void) {
    uint32_t buffer[3] = {10, 20, 30};

    bool gravou = gravarTimestampSeCouber(buffer, 3, 3, 999);

    TEST_ASSERT_FALSE(gravou);
    // buffer inalterado -- prova que nao escreveu fora dos limites
    TEST_ASSERT_EQUAL_UINT32(10, buffer[0]);
    TEST_ASSERT_EQUAL_UINT32(20, buffer[1]);
    TEST_ASSERT_EQUAL_UINT32(30, buffer[2]);
}

void test_amostra_janela_vazia_media_zero(void) {
    JanelaDePulsos janela = {};
    janela.contagem = 0;

    Amostra amostra = calcularAmostra(janela);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, amostra.avgSpeedMs);
}

void test_amostra_media_por_contagem(void) {
    // 21 pulsos em 10s = 2,1 Hz = 10 km/h (tabela de hardware.md)
    JanelaDePulsos janela = {};
    janela.contagem = 21;

    Amostra amostra = calcularAmostra(janela);

    TEST_ASSERT_FLOAT_WITHIN(0.05f, 2.77f, amostra.avgSpeedMs);
}

void test_amostra_vento_constante_rajada_igual_media(void) {
    // 10 pulsos espacados uniformemente a cada 1s, ao longo de 10s.
    uint32_t timestamps[10];
    for (int i = 0; i < 10; i++) {
        timestamps[i] = (uint32_t)(i + 1) * 1000000;  // micros
    }

    JanelaDePulsos janela = {};
    janela.contagem = 10;
    janela.timestamps = timestamps;
    janela.totalTimestamps = 10;

    Amostra amostra = calcularAmostra(janela);

    // vento constante: a rajada (pico de qualquer janela de 3s) coincide
    // com a media geral
    TEST_ASSERT_FLOAT_WITHIN(0.05f, amostra.avgSpeedMs, amostra.gustSpeedMs);
}

void test_amostra_rajada_no_meio_supera_media(void) {
    // 10 pulsos espacados a 1s (vento fraco e constante), exceto entre
    // 3s e 6s onde vem uma rajada: pulsos concentrados a cada ~0.3s.
    uint32_t timestamps[16];
    int i = 0;
    timestamps[i++] = 1000000;
    timestamps[i++] = 2000000;
    timestamps[i++] = 3000000;
    timestamps[i++] = 4000000;
    // rajada: pulsos mais frequentes entre 4s e 6s
    timestamps[i++] = 4300000;
    timestamps[i++] = 4600000;
    timestamps[i++] = 4900000;
    timestamps[i++] = 5200000;
    timestamps[i++] = 5500000;
    timestamps[i++] = 5800000;
    timestamps[i++] = 6000000;
    timestamps[i++] = 7000000;
    timestamps[i++] = 8000000;
    timestamps[i++] = 9000000;
    timestamps[i++] = 10000000;

    JanelaDePulsos janela = {};
    janela.contagem = (uint32_t)i;
    janela.timestamps = timestamps;
    janela.totalTimestamps = (uint32_t)i;

    Amostra amostra = calcularAmostra(janela);

    TEST_ASSERT_TRUE(amostra.gustSpeedMs > amostra.avgSpeedMs);

    // Pico esperado, contado a mao (janela desliza em cada indice `fim`,
    // pega o `inicio` mais a esquerda com t[fim]-t[inicio] <= 3.0s):
    // o maximo acontece em fim=10 (t=6.0s) e fim=11 (t=7.0s), ambos com
    // inicio=indice de t=3.0s ou t=4.0s respectivamente -- 8 PERIODOS
    // (9 pontos) em 3.0s = 8/3 = 2,667 Hz. Contamos periodos (pontos-1),
    // nao pontos, propositalmente -- ver o comentario do algoritmo no
    // Step 3 desta task.
    const float freqPicoEsperada = 8.0f / 3.0f;
    const float gustEsperado = 1.319f * freqPicoEsperada / PULSOS_POR_VOLTA;
    TEST_ASSERT_FLOAT_WITHIN(0.1f, gustEsperado, amostra.gustSpeedMs);
}

void test_amostra_buffer_no_limite_nao_le_fora(void) {
    // Buffer cheio (320, o cap real) com mais pulsos do que ele
    // comporta -- contagem (350) fica maior que totalTimestamps (320).
    // Confirma que calcularAmostra nao le fora do array (o teste
    // simplesmente rodar sem crash ja prova isso) e que a MEDIA usa a
    // contagem real, nao o buffer capado.
    uint32_t timestamps[320];
    for (int j = 0; j < 320; j++) {
        timestamps[j] = (uint32_t)(j + 1) * 31250;  // ~32 Hz, span ~10s
    }

    JanelaDePulsos janela = {};
    janela.contagem = 350;              // contagem real > buffer
    janela.timestamps = timestamps;
    janela.totalTimestamps = 320;       // capado
    janela.descartadosPorBuffer = 30;   // 350 - 320

    Amostra amostra = calcularAmostra(janela);

    // nao trava, nao le fora do array (o proprio teste rodar sem crash
    // ja e a prova), e a media usa CONTAGEM real, nao o buffer capado
    const float freqHzEsperada = 350 / 10.0f;
    const float avgEsperado = 1.319f * freqHzEsperada / PULSOS_POR_VOLTA;
    TEST_ASSERT_FLOAT_WITHIN(0.05f, avgEsperado, amostra.avgSpeedMs);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_periodo_para_velocidade_partida);
    RUN_TEST(test_periodo_para_velocidade_maxima);
    RUN_TEST(test_periodo_para_velocidade_10kmh);
    RUN_TEST(test_gravar_com_espaco_sobra);
    RUN_TEST(test_gravar_na_ultima_posicao_livre);
    RUN_TEST(test_gravar_buffer_cheio_nao_escreve_fora);
    RUN_TEST(test_amostra_janela_vazia_media_zero);
    RUN_TEST(test_amostra_media_por_contagem);
    RUN_TEST(test_amostra_vento_constante_rajada_igual_media);
    RUN_TEST(test_amostra_rajada_no_meio_supera_media);
    RUN_TEST(test_amostra_buffer_no_limite_nao_le_fora);
    return UNITY_END();
}
