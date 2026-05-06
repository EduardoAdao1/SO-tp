#!/usr/bin/env bash

# Testes funcionais do sistema runner/controller.

set -u

CONTROLLER="./bin/controller"
RUNNER="./bin/runner"
FIFO_SERVER="Fifo_Server"
PASTA_TESTES=".testes_funcionamento"
LOG_FILE="log.txt"

TOTAL=0
PASSOU=0
FALHOU=0
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
    PASSOU=$((PASSOU + 1))
    printf "${VERDE}[OK]${NC} %s\n" "$1"
}

erro() {
    FALHOU=$((FALHOU + 1))
    printf "${VERMELHO}[ERRO]${NC} %s\n" "$1"
}

cabecalho() {
    printf "\n${AZUL}========== %s ==========${NC}\n" "$1"
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
    rm -f "${FIFO_SERVER}" fifo_* "${LOG_FILE}"
    rm -rf "${PASTA_TESTES}"
    mkdir -p "${PASTA_TESTES}"
}

limpa_final() {
    limpa_controller
    rm -f "${FIFO_SERVER}" fifo_* "${LOG_FILE}"
}

trap limpa_final EXIT INT TERM

contem() {
    local ficheiro="$1"
    local padrao="$2"

    [ -f "${ficheiro}" ] || return 1
    grep -Eq "${padrao}" "${ficheiro}"
}

nao_contem() {
    local ficheiro="$1"
    local padrao="$2"

    [ -f "${ficheiro}" ] || return 0
    ! grep -Eq "${padrao}" "${ficheiro}"
}

inicia_controller() {
    local paralelos="$1"
    local politica="$2"

    limpa_execucao

    "${CONTROLLER}" "${paralelos}" "${politica}" \
        >"${PASTA_TESTES}/controller.out" \
        2>"${PASTA_TESTES}/controller.err" &

    PID_CONTROLLER=$!

    for _ in $(seq 1 100); do
        if [ -p "${FIFO_SERVER}" ]; then
            return 0
        fi

        if ! kill -0 "${PID_CONTROLLER}" 2>/dev/null; then
            cat "${PASTA_TESTES}/controller.err" 2>/dev/null || true
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

executa_runner() {
    local user="$1"
    local comando="$2"
    local out="$3"
    local err="$4"

    timeout 10 "${RUNNER}" -e "${user}" "${comando}" >"${out}" 2>"${err}"
}

consulta_estado() {
    local out="$1"
    local err="$2"

    timeout 5 "${RUNNER}" -c >"${out}" 2>"${err}"
}

tempo_ms() {
    python3 - <<'PY'
import time
print(int(time.time() * 1000))
PY
}

testa() {
    local nome="$1"
    shift

    TOTAL=$((TOTAL + 1))
    printf "\n${AZUL}[%02d] %s${NC}\n" "${TOTAL}" "${nome}"

    if "$@"; then
        ok "${nome}"
    else
        erro "${nome}"
        info "O teste falhou. Ficheiros temporários em: ${PASTA_TESTES}"
        exit 1
    fi
}

# ----------------------------------------------------------------------
# Testes
# ----------------------------------------------------------------------

teste_compilacao() {
    make clean >/dev/null 2>&1 || true

    mkdir -p "${PASTA_TESTES}"

    make >"${PASTA_TESTES}/make.out" 2>"${PASTA_TESTES}/make.err" || {
        cat "${PASTA_TESTES}/make.err" 2>/dev/null || true
        return 1
    }

    [ -x "${CONTROLLER}" ] && [ -x "${RUNNER}" ]
}

teste_sem_controller_nao_bloqueia() {
    limpa_execucao

    timeout 2 "${RUNNER}" -e 1 "echo teste" \
        >"${PASTA_TESTES}/sem_controller.out" \
        2>"${PASTA_TESTES}/sem_controller.err"

    [ "$?" -ne 124 ]
}

teste_echo_simples() {
    inicia_controller 1 fifo || return 1

    executa_runner 1 "echo ola" \
        "${PASTA_TESTES}/echo.out" \
        "${PASTA_TESTES}/echo.err" || return 1

    para_controller || return 1

    contem "${PASTA_TESTES}/echo.out" "\[runner\] command [0-9]+ submitted" &&
    contem "${PASTA_TESTES}/echo.out" "\[runner\] executing command [0-9]+" &&
    contem "${PASTA_TESTES}/echo.out" "^ola$" &&
    contem "${PASTA_TESTES}/echo.out" "\[runner\] command [0-9]+ finished"
}

teste_comando_com_argumentos() {
    inicia_controller 1 fifo || return 1

    executa_runner 2 "echo um dois tres" \
        "${PASTA_TESTES}/args.out" \
        "${PASTA_TESTES}/args.err" || return 1

    para_controller || return 1

    contem "${PASTA_TESTES}/args.out" "^um dois tres$"
}

teste_redirecionamento_stdout() {
    inicia_controller 1 fifo || return 1

    executa_runner 1 "echo resultado > ${PASTA_TESTES}/out.txt" \
        "${PASTA_TESTES}/redir.out" \
        "${PASTA_TESTES}/redir.err" || return 1

    para_controller || return 1

    contem "${PASTA_TESTES}/out.txt" "^resultado$" &&
    nao_contem "${PASTA_TESTES}/redir.out" "^resultado$"
}

teste_redirecionamento_stdin() {
    inicia_controller 1 fifo || return 1

    printf "abc\ndef\nabc\n" >"${PASTA_TESTES}/input.txt"

    executa_runner 1 "grep abc < ${PASTA_TESTES}/input.txt" \
        "${PASTA_TESTES}/stdin.out" \
        "${PASTA_TESTES}/stdin.err" || return 1

    para_controller || return 1

    [ "$(grep -c "^abc$" "${PASTA_TESTES}/stdin.out" || true)" -eq 2 ]
}

teste_redirecionamento_stderr() {
    inicia_controller 1 fifo || return 1

    executa_runner 1 "ls ficheiro_inexistente_123 2> ${PASTA_TESTES}/erros.txt" \
        "${PASTA_TESTES}/stderr.out" \
        "${PASTA_TESTES}/stderr.err" || true

    para_controller || return 1

    [ -s "${PASTA_TESTES}/erros.txt" ] &&
    contem "${PASTA_TESTES}/erros.txt" "ficheiro_inexistente_123"
}

teste_pipe_simples() {
    inicia_controller 1 fifo || return 1

    executa_runner 1 "printf 'a\\nb\\nc\\n' | wc -l" \
        "${PASTA_TESTES}/pipe.out" \
        "${PASTA_TESTES}/pipe.err" || return 1

    para_controller || return 1

    contem "${PASTA_TESTES}/pipe.out" "^[[:space:]]*3$"
}

teste_pipeline_com_redirecionamento() {
    inicia_controller 1 fifo || return 1

    executa_runner 1 "printf 'root\\nuser\\nroot\\n' | grep root | wc -l > ${PASTA_TESTES}/pipe_out.txt" \
        "${PASTA_TESTES}/pipeline.out" \
        "${PASTA_TESTES}/pipeline.err" || return 1

    para_controller || return 1

    contem "${PASTA_TESTES}/pipe_out.txt" "^[[:space:]]*2$"
}

teste_consulta_estado_vazio() {
    inicia_controller 1 fifo || return 1

    consulta_estado \
        "${PASTA_TESTES}/status.out" \
        "${PASTA_TESTES}/status.err" || return 1

    para_controller || return 1

    contem "${PASTA_TESTES}/status.out" "^---$" &&
    contem "${PASTA_TESTES}/status.out" "^Executing$" &&
    contem "${PASTA_TESTES}/status.out" "^Scheduled$"
}

teste_consulta_com_execucao_e_espera() {
    inicia_controller 1 fifo || return 1

    "${RUNNER}" -e 1 "sleep 2" \
        >"${PASTA_TESTES}/exec1.out" \
        2>"${PASTA_TESTES}/exec1.err" &
    local p1=$!

    sleep 0.2

    "${RUNNER}" -e 2 "sleep 1" \
        >"${PASTA_TESTES}/exec2.out" \
        2>"${PASTA_TESTES}/exec2.err" &
    local p2=$!

    sleep 0.2

    consulta_estado \
        "${PASTA_TESTES}/status_ocupado.out" \
        "${PASTA_TESTES}/status_ocupado.err" || return 1

    wait "${p1}" || true
    wait "${p2}" || true

    para_controller || return 1

    contem "${PASTA_TESTES}/status_ocupado.out" "Executing" &&
    contem "${PASTA_TESTES}/status_ocupado.out" "user-id 1 - command-id" &&
    contem "${PASTA_TESTES}/status_ocupado.out" "Scheduled" &&
    contem "${PASTA_TESTES}/status_ocupado.out" "user-id 2 - command-id"
}

teste_log_persistente() {
    inicia_controller 1 fifo || return 1

    executa_runner 5 "echo log_teste" \
        "${PASTA_TESTES}/log_runner.out" \
        "${PASTA_TESTES}/log_runner.err" || return 1

    para_controller || return 1

    # Guardamos uma cópia para debug, porque no fim do script o log.txt é apagado.
    if [ -f "${LOG_FILE}" ]; then
        cp "${LOG_FILE}" "${PASTA_TESTES}/log_copia.txt"
    fi

    [ -f "${LOG_FILE}" ] &&
    [ -s "${LOG_FILE}" ] &&
    grep -Eq "User-?[[:space:]]*5" "${LOG_FILE}" &&
    grep -Eq "Command-id-?[[:space:]]*[0-9]+" "${LOG_FILE}" &&
    grep -Eq "Duracao-?[[:space:]]*[0-9]+ms|Duração-?[[:space:]]*[0-9]+ms" "${LOG_FILE}"
}

teste_paralelismo() {
    inicia_controller 3 fifo || return 1

    local inicio fim duracao

    inicio=$(tempo_ms)

    "${RUNNER}" -e 1 "sleep 2" >"${PASTA_TESTES}/par1.out" 2>"${PASTA_TESTES}/par1.err" &
    local p1=$!

    "${RUNNER}" -e 2 "sleep 2" >"${PASTA_TESTES}/par2.out" 2>"${PASTA_TESTES}/par2.err" &
    local p2=$!

    "${RUNNER}" -e 3 "sleep 2" >"${PASTA_TESTES}/par3.out" 2>"${PASTA_TESTES}/par3.err" &
    local p3=$!

    wait "${p1}" || true
    wait "${p2}" || true
    wait "${p3}" || true

    fim=$(tempo_ms)
    duracao=$((fim - inicio))

    para_controller || return 1

    [ "${duracao}" -lt 4000 ]
}

teste_execucao_sequencial() {
    inicia_controller 1 fifo || return 1

    local inicio fim duracao

    inicio=$(tempo_ms)

    "${RUNNER}" -e 1 "sleep 1" >"${PASTA_TESTES}/seq1.out" 2>"${PASTA_TESTES}/seq1.err" &
    local p1=$!

    "${RUNNER}" -e 2 "sleep 1" >"${PASTA_TESTES}/seq2.out" 2>"${PASTA_TESTES}/seq2.err" &
    local p2=$!

    "${RUNNER}" -e 3 "sleep 1" >"${PASTA_TESTES}/seq3.out" 2>"${PASTA_TESTES}/seq3.err" &
    local p3=$!

    wait "${p1}" || true
    wait "${p2}" || true
    wait "${p3}" || true

    fim=$(tempo_ms)
    duracao=$((fim - inicio))

    para_controller || return 1

    [ "${duracao}" -ge 2500 ]
}

teste_shutdown_sem_comandos() {
    inicia_controller 1 fifo || return 1

    para_controller || return 1

    [ ! -p "${FIFO_SERVER}" ] &&
    contem "${PASTA_TESTES}/stop.out" "sent shutdown notification" &&
    contem "${PASTA_TESTES}/stop.out" "controller exited"
}

teste_shutdown_com_comando_ativo() {
    inicia_controller 1 fifo || return 1

    "${RUNNER}" -e 1 "sleep 2" \
        >"${PASTA_TESTES}/shutdown_exec.out" \
        2>"${PASTA_TESTES}/shutdown_exec.err" &
    local p_exec=$!

    sleep 0.2

    local inicio fim duracao

    inicio=$(tempo_ms)

    "${RUNNER}" -s \
        >"${PASTA_TESTES}/shutdown.out" \
        2>"${PASTA_TESTES}/shutdown.err" &
    local p_stop=$!

    wait "${p_exec}" || true
    wait "${p_stop}" || return 1

    fim=$(tempo_ms)
    duracao=$((fim - inicio))

    wait "${PID_CONTROLLER}" 2>/dev/null || true
    PID_CONTROLLER=""

    [ "${duracao}" -ge 1500 ] &&
    contem "${PASTA_TESTES}/shutdown.out" "controller exited"
}

teste_rejeicao_apos_shutdown() {
    inicia_controller 1 fifo || return 1

    "${RUNNER}" -e 1 "sleep 3" \
        >"${PASTA_TESTES}/ativo.out" \
        2>"${PASTA_TESTES}/ativo.err" &
    local p_ativo=$!

    sleep 0.2

    "${RUNNER}" -s \
        >"${PASTA_TESTES}/pedido_shutdown.out" \
        2>"${PASTA_TESTES}/pedido_shutdown.err" &
    local p_stop=$!

    sleep 0.2

    timeout 2 "${RUNNER}" -e 2 "echo nao_deve_executar" \
        >"${PASTA_TESTES}/rejeitado.out" \
        2>"${PASTA_TESTES}/rejeitado.err"
    local rc=$?

    wait "${p_ativo}" || true
    wait "${p_stop}" || true
    wait "${PID_CONTROLLER}" 2>/dev/null || true
    PID_CONTROLLER=""

    [ "${rc}" -ne 124 ] &&
    contem "${PASTA_TESTES}/rejeitado.err" "rejected" &&
    nao_contem "${PASTA_TESTES}/rejeitado.out" "nao_deve_executar"
}

teste_round_robin_basico() {
    inicia_controller 1 rr || return 1

    "${RUNNER}" -e 1 "sleep 2" \
        >"${PASTA_TESTES}/rr0.out" \
        2>"${PASTA_TESTES}/rr0.err" &
    local p0=$!

    sleep 0.2

    "${RUNNER}" -e 1 "echo user1" \
        >"${PASTA_TESTES}/rr1.out" \
        2>"${PASTA_TESTES}/rr1.err" &
    local p1=$!

    "${RUNNER}" -e 2 "echo user2" \
        >"${PASTA_TESTES}/rr2.out" \
        2>"${PASTA_TESTES}/rr2.err" &
    local p2=$!

    wait "${p0}" || true
    wait "${p1}" || true
    wait "${p2}" || true

    para_controller || return 1

    contem "${PASTA_TESTES}/rr1.out" "user1" &&
    contem "${PASTA_TESTES}/rr2.out" "user2"
}

# ----------------------------------------------------------------------
# Execução
# ----------------------------------------------------------------------

cabecalho "Testes funcionais"

limpa_execucao

testa "compilação do projeto" teste_compilacao
testa "runner sem controller não bloqueia" teste_sem_controller_nao_bloqueia

cabecalho "Execução de comandos"

testa "execução simples com echo" teste_echo_simples
testa "comando com vários argumentos" teste_comando_com_argumentos

cabecalho "Redirecionamentos e pipes"

testa "redirecionamento de stdout" teste_redirecionamento_stdout
testa "redirecionamento de stdin" teste_redirecionamento_stdin
testa "redirecionamento de stderr" teste_redirecionamento_stderr
testa "pipe simples" teste_pipe_simples
testa "pipeline com redirecionamento" teste_pipeline_com_redirecionamento

cabecalho "Consulta e log"

testa "consulta de estado sem comandos" teste_consulta_estado_vazio
testa "consulta com comando em execução e em espera" teste_consulta_com_execucao_e_espera
testa "log persistente" teste_log_persistente

cabecalho "Paralelismo"

testa "execução paralela com limite 3" teste_paralelismo
testa "execução sequencial com limite 1" teste_execucao_sequencial

cabecalho "Shutdown"

testa "shutdown sem comandos ativos" teste_shutdown_sem_comandos
testa "shutdown espera por comando ativo" teste_shutdown_com_comando_ativo
testa "comando após shutdown é rejeitado" teste_rejeicao_apos_shutdown

cabecalho "Política round-robin"

testa "round-robin básico" teste_round_robin_basico

cabecalho "Resultado final"

printf "Total: %d | Passaram: %d | Falharam: %d\n" "${TOTAL}" "${PASSOU}" "${FALHOU}"

if [ "${FALHOU}" -eq 0 ]; then
    printf "${VERDE}Todos os testes funcionais passaram.${NC}\n"
    exit 0
else
    printf "${VERMELHO}Há testes funcionais a falhar.${NC}\n"
    exit 1
fi