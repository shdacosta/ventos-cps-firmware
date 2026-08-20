#include "wifi_gerenciado.h"

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "secrets.h"
#include "wifi_transicao.h"

namespace {

// Servidor NTP publico e fuso de Campinas (America/Sao_Paulo, UTC-3,
// sem horario de verao desde 2019).
constexpr char SERVIDOR_NTP[]  = "pool.ntp.org";
constexpr long FUSO_SEGUNDOS   = -3 * 3600;

EstadoWifi estado;

void pedirSincronizacaoNtp()
{
    // configTime dispara a sincronizacao em segundo plano; nao bloqueia.
    // O resultado so aparece quando alguem ler o relogio (getLocalTime)
    // depois -- pode levar alguns segundos ate o primeiro pacote NTP
    // voltar.
    configTime(FUSO_SEGUNDOS, 0, SERVIDOR_NTP);
    estado.ntpFoiPedido = true;
    Serial.printf("[ntp] sincronizacao pedida (%s)\n", SERVIDOR_NTP);
}

}  // namespace

void iniciarWifi()
{
    // Desliga o sleep do radio: reduz consumo, mas aumenta a latencia de
    // resposta. Estacao ligada na tomada, nao a bateria -- a latencia
    // importa mais que os poucos mA economizados.
    WiFi.setSleep(false);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_SENHA);
    Serial.printf("[wifi] conectando a \"%s\"...\n", WIFI_SSID);
}

void atualizarWifi()
{
    const bool conectadoAgora = WiFi.status() == WL_CONNECTED;
    const AcaoWifi acao = avaliarTransicaoWifi(estado, conectadoAgora, millis());

    if (acao.acabouDeConectarPelaPrimeiraVez) {
        Serial.printf("[wifi] conectado. ip=%s rssi=%ddBm\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else if (acao.acabouDeReconectar) {
        Serial.printf("[wifi] reconectado (total=%lu). ip=%s rssi=%ddBm\n",
                      (unsigned long) acao.contadorReconexoesAtual,
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }

    if (acao.precisaPedirNtp) {
        pedirSincronizacaoNtp();
    }

    if (acao.deveTentarReconectarAgora) {
        Serial.printf("[wifi] desconectado, tentando reconectar (proxima espera: %lus)\n",
                      (unsigned long) (acao.esperaAtualMs / 1000));
        WiFi.disconnect();
        WiFi.reconnect();
    }
}

bool wifiConectado()
{
    return WiFi.status() == WL_CONNECTED;
}

int wifiRssiDbm()
{
    return WiFi.RSSI();
}

String wifiIp()
{
    return WiFi.localIP().toString();
}

String horaAtualFormatada()
{
    struct tm horario;

    // Timeout curto: getLocalTime tentaria esperar por uma sincronizacao
    // que ainda nao aconteceu, e isso bloquearia o loop() -- 100ms so
    // cobre o caso em que o valor ja esta pronto e e so ler.
    if (!getLocalTime(&horario, 100)) {
        return "sincronizando";
    }

    char texto[20];
    strftime(texto, sizeof(texto), "%d/%m %H:%M:%S", &horario);
    return String(texto);
}

bool relogioSincronizado()
{
    struct tm horario;
    return getLocalTime(&horario, 100);
}

time_t horaAtualUnix()
{
    if (!relogioSincronizado()) {
        return 0;
    }

    // time() le o relogio interno do ESP32, que configTime() mantem em
    // UTC de verdade -- SEM aplicar o deslocamento de fuso horario
    // (esse deslocamento so entra na conversao para struct tm, feita
    // por getLocalTime()/localtime(), nunca aqui). Por isso este valor
    // ja e o epoch UTC certo, sem precisar de mktime() nem de conta de
    // fuso horario.
    return time(nullptr);
}

uint32_t wifiContagemReconexoes()
{
    return estado.contadorReconexoes;
}
