#!/usr/bin/env bash

# Corre todos os scripts de teste e avaliação experimental.

set -u

VERDE='\033[0;32m'
VERMELHO='\033[0;31m'
AZUL='\033[0;34m'
AMARELO='\033[1;33m'
NC='\033[0m'

TOTAL=0
PASSOU=0
FALHOU=0

corre_script() {
    local nome="$1"
    local caminho="$2"

    TOTAL=$((TOTAL + 1))

    printf "\n${AZUL}========== %s ==========${NC}\n" "${nome}"

    if [ ! -x "${caminho}" ]; then
        printf "${AMARELO}[INFO]${NC} A dar permissão de execução a %s\n" "${caminho}"
        chmod +x "${caminho}" || {
            printf "${VERMELHO}[ERRO]${NC} Não foi possível dar permissão a %s\n" "${caminho}"
            FALHOU=$((FALHOU + 1))
            return 1
        }
    fi

    if "${caminho}"; then
        printf "${VERDE}[OK]${NC} %s terminou com sucesso\n" "${nome}"
        PASSOU=$((PASSOU + 1))
        return 0
    else
        printf "${VERMELHO}[ERRO]${NC} %s falhou\n" "${nome}"
        FALHOU=$((FALHOU + 1))
        return 1
    fi
}

printf "${AZUL}A correr todos os testes e avaliações.${NC}\n"

corre_script "Testes funcionais" "./scripts/testa_funcionamento.sh" || exit 1
corre_script "Avaliação de paralelismo" "./scripts/avalia_paralelismo.sh" || exit 1
corre_script "Comparação de políticas" "./scripts/compara_politicas.sh" || exit 1

printf "\n${AZUL}========== Resultado final ==========${NC}\n"
printf "Scripts corridos: %d | Passaram: %d | Falharam: %d\n" "${TOTAL}" "${PASSOU}" "${FALHOU}"

if [ "${FALHOU}" -eq 0 ]; then
    printf "${VERDE}Todos os scripts terminaram com sucesso.${NC}\n"
    printf "Resultados gerados em: results/\n"
    exit 0
else
    printf "${VERMELHO}Há scripts a falhar.${NC}\n"
    exit 1
fi