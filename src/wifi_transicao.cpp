#include "wifi_transicao.h"

AcaoWifi avaliarTransicaoWifi(EstadoWifi& estado, bool conectadoAgora, uint32_t agoraMs)
{
    AcaoWifi acao;

    if (conectadoAgora) {
        if (!estado.jaConectouAlgumaVez) {
            estado.jaConectouAlgumaVez = true;
            estado.backoffAtualMs = BACKOFF_INICIAL_MS;
            acao.acabouDeConectarPelaPrimeiraVez = true;
        } else if (estado.caiuDesdeAUltimaConexao) {
            estado.contadorReconexoes++;
            estado.caiuDesdeAUltimaConexao = false;
            estado.backoffAtualMs = BACKOFF_INICIAL_MS;
            acao.acabouDeReconectar = true;
            acao.contadorReconexoesAtual = estado.contadorReconexoes;
        }
        if (!estado.ntpFoiPedido) {
            acao.precisaPedirNtp = true;
        }
        return acao;
    }

    if (estado.jaConectouAlgumaVez) {
        estado.ntpFoiPedido = false;
        estado.caiuDesdeAUltimaConexao = true;
    }

    if (agoraMs < estado.proximaTentativaEm) {
        return acao;  // ainda dentro da janela de espera do backoff
    }

    acao.deveTentarReconectarAgora = true;
    acao.esperaAtualMs = estado.backoffAtualMs;

    estado.proximaTentativaEm = agoraMs + estado.backoffAtualMs;
    estado.backoffAtualMs *= 2;
    if (estado.backoffAtualMs > BACKOFF_MAXIMO_MS) {
        estado.backoffAtualMs = BACKOFF_MAXIMO_MS;
    }

    return acao;
}
