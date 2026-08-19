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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_periodo_para_velocidade_partida);
    RUN_TEST(test_periodo_para_velocidade_maxima);
    RUN_TEST(test_periodo_para_velocidade_10kmh);
    RUN_TEST(test_gravar_com_espaco_sobra);
    RUN_TEST(test_gravar_na_ultima_posicao_livre);
    RUN_TEST(test_gravar_buffer_cheio_nao_escreve_fora);
    return UNITY_END();
}
