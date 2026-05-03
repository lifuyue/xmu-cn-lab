#!/usr/bin/env bash
set -euo pipefail

PORT="${PORT:-19107}"
DB="tmp/report_demo.db"
SERVER_LOG="tmp/report_server.log"
SERVER_PID=""
SERIAL=""

cleanup() {
    if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" 2>/dev/null; then
        kill "${SERVER_PID}" 2>/dev/null || true
        for _ in 1 2 3 4 5; do
            if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
                break
            fi
            sleep 0.2
        done
        if kill -0 "${SERVER_PID}" 2>/dev/null; then
            kill -9 "${SERVER_PID}" 2>/dev/null || true
        fi
        wait "${SERVER_PID}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

start_server() {
    mkdir -p tmp
    ./license_server --port "${PORT}" --db "${DB}" --timeout 5 > "${SERVER_LOG}" 2>&1 &
    SERVER_PID="$!"
    sleep 1
}

fresh_server() {
    mkdir -p tmp
    rm -f "${DB}" "${SERVER_LOG}" tmp/report_pc*.log tmp/report_restart.log
    start_server
}

issue_license() {
    local output serial
    output="$(./software_a admin --user xmu --password network2026 --type 2 --port "${PORT}")"
    printf '%s\n' "${output}"
    serial="$(printf '%s\n' "${output}" | awk '/server reply: OK SERIAL/ {print $5}')"
    printf 'demo serial: %s\n' "${serial}"
    SERIAL="${serial}"
}

show_title() {
    printf '\n===== %s =====\n' "$1"
}

case "${1:-}" in
    build)
        show_title "compile"
        make clean
        make
        ls -lh license_server software_a
        ;;

    issue)
        make -s
        fresh_server
        show_title "issue license"
        issue_license
        show_title "status after issue"
        ./software_a status --port "${PORT}"
        show_title "server log"
        sed -n '1,8p' "${SERVER_LOG}"
        ;;

    concurrency)
        make -s
        fresh_server
        show_title "issue two-user license"
        issue_license

        show_title "start pc01 and pc02, then verify pc03"
        ./software_a run --serial "${SERIAL}" --client-id pc01 --heartbeat 1 --hold 5 --port "${PORT}" > tmp/report_pc01.log &
        pc01_pid="$!"
        ./software_a run --serial "${SERIAL}" --client-id pc02 --heartbeat 1 --hold 5 --port "${PORT}" > tmp/report_pc02.log &
        pc02_pid="$!"
        sleep 1
        ./software_a run --serial "${SERIAL}" --client-id pc03 --heartbeat 1 --hold 1 --port "${PORT}" || true
        wait "${pc01_pid}"
        wait "${pc02_pid}"

        show_title "pc01 log"
        sed -n '1,5p' tmp/report_pc01.log
        tail -n 1 tmp/report_pc01.log
        show_title "pc02 log"
        sed -n '1,5p' tmp/report_pc02.log
        tail -n 1 tmp/report_pc02.log
        ;;

    timeout)
        make -s
        fresh_server
        show_title "issue license"
        issue_license

        show_title "abnormal exit without release"
        ./software_a run --serial "${SERIAL}" --client-id crash01 --heartbeat 1 --hold 2 --no-release --port "${PORT}"
        show_title "status before timeout"
        ./software_a status --port "${PORT}"
        sleep 6
        show_title "status after timeout"
        ./software_a status --port "${PORT}"
        ;;

    restart)
        make -s
        fresh_server
        show_title "issue license"
        issue_license

        show_title "client keeps retrying while server restarts"
        ./software_a run --serial "${SERIAL}" --client-id restart01 --heartbeat 1 --hold 8 --port "${PORT}" > tmp/report_restart.log &
        client_pid="$!"
        sleep 3
        kill "${SERVER_PID}"
        wait "${SERVER_PID}" 2>/dev/null || true
        SERVER_PID=""
        sleep 2
        start_server
        wait "${client_pid}"
        sed -n '1,12p' tmp/report_restart.log
        tail -n 1 tmp/report_restart.log
        ;;

    *)
        echo "Usage: $0 {build|issue|concurrency|timeout|restart}"
        exit 1
        ;;
esac
