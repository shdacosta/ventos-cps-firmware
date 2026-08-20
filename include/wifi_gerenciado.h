#pragma once

#include <WString.h>
#include <ctime>

// Conecta e mantem a conexao Wi-Fi viva, e o relogio sincronizado via
// NTP, sem nunca bloquear o loop().
//
// Chame iniciarWifi() uma vez no setup(), e atualizarWifi() a cada
// iteracao do loop() -- ela e rapida (so verifica se ja passou tempo
// suficiente desde a ultima tentativa) e nunca espera (delay()) por
// nada. Reconecta sozinha com backoff exponencial se a rede cair.
void iniciarWifi();
void atualizarWifi();

bool   wifiConectado();
int    wifiRssiDbm();
String wifiIp();

// "sincronizando" enquanto o NTP ainda nao respondeu; senao a hora
// local no formato dd/mm HH:MM:SS.
String horaAtualFormatada();

// Verdadeiro assim que o NTP sincronizou pela primeira vez desde o
// boot -- mesmo mecanismo que horaAtualFormatada() ja usa (getLocalTime
// com timeout curto, nunca bloqueia o loop() esperando).
bool relogioSincronizado();

// Epoch UTC em segundos. Devolve 0 se o relogio ainda nao sincronizou --
// quem chama deve checar relogioSincronizado() antes de usar o valor
// para algo que importa (nunca grave um measured_at baseado num retorno
// de 0, o backend rejeitaria mesmo).
time_t horaAtualUnix();

// Reconexoes de verdade desde o boot -- NAO conta a conexao inicial.
// Reseta a cada reinicio (mesmo padrao de uptime_seconds, que tambem
// zera no boot -- os dois sao "desde quando o dispositivo esta de pe").
uint32_t wifiContagemReconexoes();
