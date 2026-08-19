#pragma once

#include <cstdint>

// Trocar aqui quando o dupont confirmar pulsos/volta
// (specs/pendencias-hardware.md #1). Ver a nota de sinal em
// periodoParaVelocidadeMs sobre por que multiplicar, nao dividir.
constexpr float PULSOS_POR_VOLTA = 1.0f;

// v(m/s) = 1319 / (T(ms) * PULSOS_POR_VOLTA).
//
// Por que multiplicar T por PULSOS_POR_VOLTA, nao dividir o resultado:
// com 2 pulsos/volta, o periodo OBSERVADO entre pulsos e a METADE do
// periodo de uma rotacao completa (2 eventos por volta, nao 1) -- entao
// o periodo real de rotacao e T_observado * PULSOS_POR_VOLTA. Aplicar a
// constante sobre esse periodo corrigido da a velocidade certa direto.
float periodoParaVelocidadeMs(uint32_t periodoMicros);
