#include "wifi_gerenciado.h"

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "secrets.h"

namespace {

// Nunca espera mais que isso entre tentativas de reconexao.
constexpr uint32_t BACKOFF_INICIAL_MS = 500;
constexpr uint32_t BACKOFF_MAXIMO_MS  = 30000;

// Servidor NTP publico e fuso de Campinas (America/Sao_Paulo, UTC-3,
// sem horario de verao desde 2019).
constexpr char SERVIDOR_NTP[]  = "pool.ntp.org";
constexpr long FUSO_SEGUNDOS   = -3 * 3600;

uint32_t proximaTentativaEm = 0;
uint32_t backoffAtualMs     = BACKOFF_INICIAL_MS;
bool     jaConectouAlgumaVez = false;
bool     ntpFoiPedido        = false;

void pedirSincronizacaoNtp()
{
    // configTime dispara a sincronizacao em segundo plano; nao bloqueia.
    // O resultado so aparece quando alguem ler o relogio (getLocalTime)
    // depois -- pode levar alguns segundos ate o primeiro pacote NTP
    // voltar.
    configTime(FUSO_SEGUNDOS, 0, SERVIDOR_NTP);
    ntpFoiPedido = true;
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
    if (WiFi.status() == WL_CONNECTED) {
        if (!jaConectouAlgumaVez) {
            jaConectouAlgumaVez = true;
            backoffAtualMs = BACKOFF_INICIAL_MS;  // reseta para a proxima queda
            Serial.printf("[wifi] conectado. ip=%s rssi=%ddBm\n",
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());
        }
        if (!ntpFoiPedido) {
            pedirSincronizacaoNtp();
        }
        return;
    }

    // Perdeu a conexao depois de ja ter conectado uma vez -- reseta o
    // estado do NTP. Uma reconexao pode levar minutos, e o ESP32 nao
    // tem RTC com bateria propria: sem Wi-Fi, o relogio so vai derivando.
    if (jaConectouAlgumaVez) {
        ntpFoiPedido = false;
    }

    const uint32_t agora = millis();
    if (agora < proximaTentativaEm) {
        return;  // ainda dentro da janela de espera do backoff
    }

    Serial.printf("[wifi] desconectado, tentando reconectar (proxima espera: %lus)\n",
                  (unsigned long) (backoffAtualMs / 1000));
    WiFi.disconnect();
    WiFi.reconnect();

    proximaTentativaEm = agora + backoffAtualMs;

    backoffAtualMs *= 2;
    if (backoffAtualMs > BACKOFF_MAXIMO_MS) {
        backoffAtualMs = BACKOFF_MAXIMO_MS;
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
