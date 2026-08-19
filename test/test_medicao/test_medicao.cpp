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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_periodo_para_velocidade_partida);
    RUN_TEST(test_periodo_para_velocidade_maxima);
    RUN_TEST(test_periodo_para_velocidade_10kmh);
    return UNITY_END();
}
