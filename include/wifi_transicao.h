#pragma once
#include <cstdint>

// Nunca espera mais que isso entre tentativas de reconexao.
constexpr uint32_t BACKOFF_INICIAL_MS = 500;
constexpr uint32_t BACKOFF_MAXIMO_MS  = 30000;

// Todo o estado que a maquina de transicao do Wi-Fi precisa entre
// chamadas -- sem nada de WiFi.h/Arduino.h, testavel em env:native.
struct EstadoWifi {
    uint32_t proximaTentativaEm      = 0;
    uint32_t backoffAtualMs          = BACKOFF_INICIAL_MS;
    bool     jaConectouAlgumaVez     = false;
    bool     ntpFoiPedido            = false;
    uint32_t contadorReconexoes      = 0;
    bool     caiuDesdeAUltimaConexao = false;
};

// O que o chamador (atualizarWifi(), que toca WiFi.h de verdade) precisa
// fazer depois de avaliarTransicaoWifi() decidir -- so os efeitos
// colaterais ficam de fora desta funcao; tudo o resto (contagem,
// backoff, quando pedir NTP, quando tentar reconectar) e decidido aqui.
struct AcaoWifi {
    bool     acabouDeConectarPelaPrimeiraVez = false;
    bool     acabouDeReconectar              = false;
    bool     precisaPedirNtp                 = false;
    bool     deveTentarReconectarAgora       = false;
    uint32_t contadorReconexoesAtual         = 0;  // valor apos a transicao, pra log
    uint32_t esperaAtualMs                   = 0;  // backoff em uso NESTA tentativa, pra log (so importa quando deveTentarReconectarAgora)
};

// Decide a transicao de estado do Wi-Fi a partir de um unico booleano
// (WiFi.status()==WL_CONNECTED, ja calculado por quem chama) e do
// instante atual (millis(), idem) -- nao toca hardware nenhum, so
// EstadoWifi. Muta `estado` in-place e devolve o que fazer.
AcaoWifi avaliarTransicaoWifi(EstadoWifi& estado, bool conectadoAgora, uint32_t agoraMs);
