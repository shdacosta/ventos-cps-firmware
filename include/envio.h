#pragma once

#include "medicao.h"

// Converte a Amostra (ja calculada pela Fase 2+3) num registro de
// telemetria e guarda no buffer -- so se o relogio (NTP) ja estiver
// sincronizado, para nunca guardar measured_at invalido (o backend
// rejeitaria mesmo).
void registrarAmostra(const Amostra& amostra);

// Tenta esvaziar o buffer de telemetria via POST /api/v1/ingest, em ate
// MAX_LOTES_POR_CICLO lotes. So faz alguma coisa se o Wi-Fi estiver
// conectado -- senao, os dados continuam acumulando no buffer para a
// proxima tentativa.
void tentarEnviarLotes();
