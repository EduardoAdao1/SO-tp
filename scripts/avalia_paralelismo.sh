#!/usr/bin/env bash

# Avaliação experimental do impacto do número máximo de comandos paralelos.

set -u

CONTROLLER="./bin/controller"
RUNNER="./bin/runner"
FIFO_SERVER="Fifo_Server"

PASTA_TESTES=".avalia_paralelismo"
PASTA_RESULTADOS="results"
CSV="${PASTA_RESULTADOS}/paralelismo.csv"

POLITICA="fifo"
NUM_COMANDOS=8
DURACAO_SLEEP=2

PID_CONTROLLER=""

VERDE='\033[0;32m'
VERMELHO='\033[0;31m'
AZUL='\033[0;34m'
AMARELO='\033[1;33m'
NC='\033[0m'

info() {
    printf "${AMARELO}[INFO]${NC} %s\n" "$1"
}

ok() {
    printf "${VERDE}[OK]${NC} %s\n" "$1"
}

erro() {
    printf "${VERMELHO}[ERRO]${NC} %s\n" "$1"
}

cabecalho() {
    printf "\n${AZUL}========== %s ==========${NC}\n" "$1"
}

tempo_ms() {
    python3 - <<'PY'
import time
print(int(time.time() * 1000))
PY
}

limpa_controller() {
    if [ -n "${PID_CONTROLLER}" ]; then
        if kill -0 "${PID_CONTROLLER}" 2>/dev/null; then
            kill "${PID_CONTROLLER}" 2>/dev/null || true
            sleep 0.1
            kill -9 "${PID_CONTROLLER}" 2>/dev/null || true
            wait "${PID_CONTROLLER}" 2>/dev/null || true
        fi
    fi

    PID_CONTROLLER=""

    killall controller 2>/dev/null || true
}

limpa_execucao() {
    limpa_controller
    rm -f "${FIFO_SERVER}" fifo_* log.txt
    rm -rf "${PASTA_TESTES}"
    mkdir -p "${PASTA_TESTES}"
}

limpa_final() {
    limpa_controller
    rm -f "${FIFO_SERVER}" fifo_* log.txt
}

trap limpa_final EXIT INT TERM

compila() {
    make >"${PASTA_TESTES}/make.out" 2>"${PASTA_TESTES}/make.err" || {
        cat "${PASTA_TESTES}/make.err"
        return 1
    }

    [ -x "${CONTROLLER}" ] && [ -x "${RUNNER}" ]
}

inicia_controller() {
    local paralelos="$1"

    limpa_execucao

    "${CONTROLLER}" "${paralelos}" "${POLITICA}" \
        >"${PASTA_TESTES}/controller_${paralelos}.out" \
        2>"${PASTA_TESTES}/controller_${paralelos}.err" &

    PID_CONTROLLER=$!

    for _ in $(seq 1 100); do
        if [ -p "${FIFO_SERVER}" ]; then
            return 0
        fi

        if ! kill -0 "${PID_CONTROLLER}" 2>/dev/null; then
            cat "${PASTA_TESTES}/controller_${paralelos}.err" 2>/dev/null || true
            return 1
        fi

        sleep 0.03
    done

    return 1
}

para_controller() {
    if [ -n "${PID_CONTROLLER}" ] && kill -0 "${PID_CONTROLLER}" 2>/dev/null; then
        timeout 8 "${RUNNER}" -s \
            >"${PASTA_TESTES}/stop.out" \
            2>"${PASTA_TESTES}/stop.err" || return 1

        wait "${PID_CONTROLLER}" 2>/dev/null || true
    fi

    PID_CONTROLLER=""
    return 0
}

executa_configuracao() {
    local paralelos="$1"
    local inicio fim duracao
    local pids=""
    local user

    cabecalho "parallel-commands = ${paralelos}"

    inicia_controller "${paralelos}" || {
        erro "não foi possível iniciar o controller"
        return 1
    }

    inicio=$(tempo_ms)

    for i in $(seq 1 "${NUM_COMANDOS}"); do
        user=$(( (i % 4) + 1 ))

        "${RUNNER}" -e "${user}" "sleep ${DURACAO_SLEEP}" \
            >"${PASTA_TESTES}/runner_p${paralelos}_${i}.out" \
            2>"${PASTA_TESTES}/runner_p${paralelos}_${i}.err" &

        pids="${pids} $!"
    done

    for p in ${pids}; do
        wait "${p}" || true
    done

    fim=$(tempo_ms)
    duracao=$((fim - inicio))

    para_controller || {
        erro "não foi possível parar o controller"
        return 1
    }

    printf "%s,%d,%d,%d,%d\n" \
        "${POLITICA}" \
        "${paralelos}" \
        "${NUM_COMANDOS}" \
        "${DURACAO_SLEEP}" \
        "${duracao}" >> "${CSV}"

    ok "paralelos=${paralelos} | comandos=${NUM_COMANDOS} | duração=${duracao}ms"

    return 0
}

mostra_resultados() {
    cabecalho "Resultados"

    column -s, -t "${CSV}" 2>/dev/null || cat "${CSV}"

    printf "\nFicheiro gerado: %s\n" "${CSV}"
}

# ----------------------------------------------------------------------
# Execução
# ----------------------------------------------------------------------

cabecalho "Avaliação de paralelismo"

mkdir -p "${PASTA_TESTES}"
mkdir -p "${PASTA_RESULTADOS}"

info "A compilar o projeto..."
compila || {
    erro "falha na compilação"
    exit 1
}

cat > "${CSV}" <<EOF
politica,paralelos,comandos,duracao_sleep_s,duracao_total_ms
EOF

info "Política usada: ${POLITICA}"
info "Número de comandos por teste: ${NUM_COMANDOS}"
info "Cada comando executa: sleep ${DURACAO_SLEEP}"

executa_configuracao 1 || exit 1
executa_configuracao 2 || exit 1
executa_configuracao 4 || exit 1
executa_configuracao 8 || exit 1

mostra_resultados

printf "\n${VERDE}Avaliação de paralelismo concluída.${NC}\n"
exit 0