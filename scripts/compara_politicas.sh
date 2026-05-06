#!/usr/bin/env bash

# Comparação experimental das políticas de escalonamento.

set -u

CONTROLLER="./bin/controller"
RUNNER="./bin/runner"
FIFO_SERVER="Fifo_Server"

PASTA_TESTES=".compara_politicas"
PASTA_RESULTADOS="results"
CSV="${PASTA_RESULTADOS}/politicas.csv"

PARALELOS=1
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
    local politica="$1"

    limpa_execucao

    "${CONTROLLER}" "${PARALELOS}" "${politica}" \
        >"${PASTA_TESTES}/controller_${politica}.out" \
        2>"${PASTA_TESTES}/controller_${politica}.err" &

    PID_CONTROLLER=$!

    for _ in $(seq 1 100); do
        if [ -p "${FIFO_SERVER}" ]; then
            return 0
        fi

        if ! kill -0 "${PID_CONTROLLER}" 2>/dev/null; then
            cat "${PASTA_TESTES}/controller_${politica}.err" 2>/dev/null || true
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

extrai_command_id() {
    local ficheiro="$1"

    grep -Eo "command [0-9]+ submitted" "${ficheiro}" | head -n 1 | grep -Eo "[0-9]+"
}

tempo_fim_relativo() {
    local inicio="$1"
    local fim="$2"

    echo $((fim - inicio))
}

guarda_resultado() {
    local politica="$1"
    local user="$2"
    local nome="$3"
    local comando="$4"
    local inicio_ms="$5"
    local fim_ms="$6"
    local out_file="$7"

    local espera_total
    local command_id

    espera_total=$(tempo_fim_relativo "${inicio_ms}" "${fim_ms}")
    command_id=$(extrai_command_id "${out_file}")

    if [ -z "${command_id}" ]; then
        command_id="-"
    fi

    printf "%s,%s,%s,%s,%s,%d\n" \
        "${politica}" \
        "${user}" \
        "${nome}" \
        "${command_id}" \
        "${comando}" \
        "${espera_total}" >> "${CSV}"
}

executa_runner_medido() {
    local politica="$1"
    local user="$2"
    local nome="$3"
    local comando="$4"
    local inicio_global="$5"

    local out_file="${PASTA_TESTES}/${politica}_${nome}.out"
    local err_file="${PASTA_TESTES}/${politica}_${nome}.err"

    "${RUNNER}" -e "${user}" "${comando}" >"${out_file}" 2>"${err_file}"
    local fim
    fim=$(tempo_ms)

    guarda_resultado \
        "${politica}" \
        "${user}" \
        "${nome}" \
        "${comando}" \
        "${inicio_global}" \
        "${fim}" \
        "${out_file}"
}

executa_cenario() {
    local politica="$1"
    local inicio_global
    local pids=""

    cabecalho "Política ${politica}"

    inicia_controller "${politica}" || {
        erro "não foi possível iniciar o controller com política ${politica}"
        return 1
    }

    inicio_global=$(tempo_ms)

    # O user 1 submete vários comandos primeiro.
    # Depois os users 2 e 3 submetem comandos curtos.
    # Com FIFO, os users 2 e 3 tendem a esperar pelos comandos anteriores do user 1.
    # Com RR, devem ser escolhidos mais cedo quando houver vaga.

    executa_runner_medido "${politica}" 1 "u1_a" "sleep 2" "${inicio_global}" &
    pids="${pids} $!"

    sleep 0.10

    executa_runner_medido "${politica}" 1 "u1_b" "sleep 2" "${inicio_global}" &
    pids="${pids} $!"

    sleep 0.10

    executa_runner_medido "${politica}" 1 "u1_c" "sleep 2" "${inicio_global}" &
    pids="${pids} $!"

    sleep 0.10

    executa_runner_medido "${politica}" 2 "u2_a" "echo user2" "${inicio_global}" &
    pids="${pids} $!"

    sleep 0.10

    executa_runner_medido "${politica}" 3 "u3_a" "echo user3" "${inicio_global}" &
    pids="${pids} $!"

    for p in ${pids}; do
        wait "${p}" || true
    done

    para_controller || {
        erro "não foi possível parar o controller"
        return 1
    }

    ok "cenário com política ${politica} concluído"
    return 0
}

mostra_resultados() {
    cabecalho "Resultados"

    column -s, -t "${CSV}" 2>/dev/null || cat "${CSV}"

    printf "\nFicheiro gerado: %s\n" "${CSV}"

    printf "\nResumo por utilizador:\n"
    awk -F, '
        NR > 1 {
            chave = $1 "," $2
            soma[chave] += $6
            conta[chave] += 1
        }
        END {
            print "politica,user,media_tempo_total_ms"
            for (chave in soma) {
                print chave "," int(soma[chave] / conta[chave])
            }
        }
    ' "${CSV}" | column -s, -t 2>/dev/null || true
}

# ----------------------------------------------------------------------
# Execução
# ----------------------------------------------------------------------

cabecalho "Comparação de políticas"

mkdir -p "${PASTA_TESTES}"
mkdir -p "${PASTA_RESULTADOS}"

info "A compilar o projeto..."
compila || {
    erro "falha na compilação"
    exit 1
}

cat > "${CSV}" <<EOF
politica,user,nome,command_id,comando,tempo_total_ms
EOF

info "Paralelismo usado: ${PARALELOS}"
info "Políticas comparadas: fifo e rr"

executa_cenario "fifo" || exit 1
executa_cenario "rr" || exit 1

mostra_resultados

printf "\n${VERDE}Comparação de políticas concluída.${NC}\n"
exit 0