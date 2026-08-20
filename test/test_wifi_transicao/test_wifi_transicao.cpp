#include <unity.h>

#include "wifi_transicao.h"

void test_conexao_inicial_nao_incrementa_contador(void) {
    EstadoWifi estado;

    AcaoWifi acao = avaliarTransicaoWifi(estado, true, 1000);

    TEST_ASSERT_TRUE(acao.acabouDeConectarPelaPrimeiraVez);
    TEST_ASSERT_FALSE(acao.acabouDeReconectar);
    TEST_ASSERT_TRUE(acao.precisaPedirNtp);
    TEST_ASSERT_EQUAL_UINT32(0, estado.contadorReconexoes);
    TEST_ASSERT_EQUAL_UINT32(BACKOFF_INICIAL_MS, estado.backoffAtualMs);
    TEST_ASSERT_TRUE(estado.jaConectouAlgumaVez);
}

void test_queda_unica_reconecta_incrementa_uma_vez(void) {
    EstadoWifi estado;
    avaliarTransicaoWifi(estado, true, 1000);
    estado.ntpFoiPedido = true;

    avaliarTransicaoWifi(estado, false, 2000);

    AcaoWifi acao = avaliarTransicaoWifi(estado, true, 2600);

    TEST_ASSERT_TRUE(acao.acabouDeReconectar);
    TEST_ASSERT_FALSE(acao.acabouDeConectarPelaPrimeiraVez);
    TEST_ASSERT_EQUAL_UINT32(1, acao.contadorReconexoesAtual);
    TEST_ASSERT_EQUAL_UINT32(1, estado.contadorReconexoes);
}

void test_queda_longa_com_tentativas_falhas_conta_como_uma_reconexao(void) {
    EstadoWifi estado;
    avaliarTransicaoWifi(estado, true, 0);

    avaliarTransicaoWifi(estado, false, 1000);
    avaliarTransicaoWifi(estado, false, 2000);
    avaliarTransicaoWifi(estado, false, 5000);
    TEST_ASSERT_EQUAL_UINT32(0, estado.contadorReconexoes);

    AcaoWifi acao = avaliarTransicaoWifi(estado, true, 10000);

    TEST_ASSERT_TRUE(acao.acabouDeReconectar);
    TEST_ASSERT_EQUAL_UINT32(1, estado.contadorReconexoes);
}

void test_piscar_de_status_conta_como_reconexao_comportamento_conhecido(void) {
    // Documenta o comportamento ja conhecido e registrado em
    // specs/pendencias-hardware.md #7: esta funcao decide so a partir do
    // booleano WiFi.status()==WL_CONNECTED que o chamador ja calculou --
    // ela nao tem como distinguir uma queda de sinal real de um "piscar"
    // de um unico tick sem queda de verdade. Este teste nao prova que o
    // radio real pisca (isso exige hardware, ver pendencias-hardware.md
    // #7); prova que SE ele piscar, o efeito na maquina de estados e
    // contar como reconexao -- caracteristica conhecida, nao bug desta
    // extracao.
    EstadoWifi estado;
    avaliarTransicaoWifi(estado, true, 0);

    avaliarTransicaoWifi(estado, false, 100);
    AcaoWifi acao = avaliarTransicaoWifi(estado, true, 200);

    TEST_ASSERT_TRUE(acao.acabouDeReconectar);
    TEST_ASSERT_EQUAL_UINT32(1, estado.contadorReconexoes);
}

void test_backoff_reseta_apos_reconexao_de_verdade(void) {
    EstadoWifi estado;
    avaliarTransicaoWifi(estado, true, 0);
    avaliarTransicaoWifi(estado, false, 1000);
    avaliarTransicaoWifi(estado, false, 1500);
    TEST_ASSERT_EQUAL_UINT32(2000, estado.backoffAtualMs);

    avaliarTransicaoWifi(estado, true, 2600);

    TEST_ASSERT_EQUAL_UINT32(BACKOFF_INICIAL_MS, estado.backoffAtualMs);
}

void test_backoff_dobra_e_respeita_teto(void) {
    EstadoWifi estado;
    avaliarTransicaoWifi(estado, true, 0);

    uint32_t agora = 1000;
    for (int i = 0; i < 6; i++) {
        AcaoWifi acao = avaliarTransicaoWifi(estado, false, agora);
        agora += acao.esperaAtualMs + 1;
    }

    TEST_ASSERT_EQUAL_UINT32(BACKOFF_MAXIMO_MS, estado.backoffAtualMs);
}

void test_nao_tenta_reconectar_antes_da_janela_de_backoff(void) {
    EstadoWifi estado;
    avaliarTransicaoWifi(estado, true, 0);
    avaliarTransicaoWifi(estado, false, 1000);

    AcaoWifi acao = avaliarTransicaoWifi(estado, false, 1200);

    TEST_ASSERT_FALSE(acao.deveTentarReconectarAgora);
}

void test_ntp_nao_e_pedido_de_novo_enquanto_conectado(void) {
    EstadoWifi estado;
    AcaoWifi primeira = avaliarTransicaoWifi(estado, true, 0);
    TEST_ASSERT_TRUE(primeira.precisaPedirNtp);

    estado.ntpFoiPedido = true;

    AcaoWifi segunda = avaliarTransicaoWifi(estado, true, 1000);
    TEST_ASSERT_FALSE(segunda.precisaPedirNtp);
}

void test_ntp_e_pedido_de_novo_apos_reconexao(void) {
    EstadoWifi estado;
    avaliarTransicaoWifi(estado, true, 0);
    estado.ntpFoiPedido = true;

    avaliarTransicaoWifi(estado, false, 1000);

    AcaoWifi acao = avaliarTransicaoWifi(estado, true, 1600);

    TEST_ASSERT_TRUE(acao.precisaPedirNtp);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_conexao_inicial_nao_incrementa_contador);
    RUN_TEST(test_queda_unica_reconecta_incrementa_uma_vez);
    RUN_TEST(test_queda_longa_com_tentativas_falhas_conta_como_uma_reconexao);
    RUN_TEST(test_piscar_de_status_conta_como_reconexao_comportamento_conhecido);
    RUN_TEST(test_backoff_reseta_apos_reconexao_de_verdade);
    RUN_TEST(test_backoff_dobra_e_respeita_teto);
    RUN_TEST(test_nao_tenta_reconectar_antes_da_janela_de_backoff);
    RUN_TEST(test_ntp_nao_e_pedido_de_novo_enquanto_conectado);
    RUN_TEST(test_ntp_e_pedido_de_novo_apos_reconexao);
    return UNITY_END();
}
