#pragma once

#include <WString.h>

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
