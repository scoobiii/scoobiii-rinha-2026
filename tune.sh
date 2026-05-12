#!/bin/bash
set -e

PROJECT_DIR=~/scoobiii-rinha-2026
cd "$PROJECT_DIR"

BACKUP_DIR="backup_config_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$BACKUP_DIR"
cp src/ivf.c src/main.c nginx-termux.conf "$BACKUP_DIR/"

RESULT_FILE="tuning_results.csv"
echo "nprobe,thread_pool,keepalive_timeout,p99_ms,error_rate,score" > "$RESULT_FILE"

replace_param() {
    local file=$1
    local pattern=$2
    local new_value=$3
    sed -i "s/$pattern/$new_value/" "$file"
}

run_wrk() {
    local output=$(wrk -t2 -c10 -d30s --latency -s ~/fraud_test.lua http://localhost:9999/fraud-score 2>&1)
    # Extrai p99 (linha "99%    X.XXms")
    local p99=$(echo "$output" | awk '/99%/{print $2}' | sed 's/ms//')
    # Extrai número de respostas não-2xx (linha "Non-2xx or 3xx responses: N")
    local non2xx=$(echo "$output" | grep -oP 'Non-2xx or 3xx responses:\s+\K\d+')
    # Extrai total de requisições (linha "N requests in ...")
    local total_req=$(echo "$output" | grep -oP '\d+(?= requests in)')
    if [ -z "$p99" ]; then p99=100; fi
    if [ -z "$non2xx" ]; then non2xx=0; fi
    if [ -z "$total_req" ]; then total_req=1; fi
    local error_rate=$(echo "scale=6; $non2xx * 100 / $total_req" | bc)
    echo "$p99 $error_rate"
}

calc_score() {
    local p99=$1
    local err_rate=$2
    local lat_penalty=$(echo "scale=2; 30 * ($p99 - 1)" | bc)
    if (( $(echo "$lat_penalty < 0" | bc -l) )); then lat_penalty=0; fi
    local err_penalty=$(echo "scale=2; 500 * $err_rate" | bc)
    local score=$(echo "scale=2; 6000 - $lat_penalty - $err_penalty" | bc)
    if (( $(echo "$score < -6000" | bc -l) )); then score=-6000; fi
    if (( $(echo "$score > 6000" | bc -l) )); then score=6000; fi
    echo "$score"
}

test_config() {
    local nprobe=$1
    local thread_pool=$2
    local keepalive_to=$3

    echo "=== Testando nprobe=$nprobe, THREAD_POOL=$thread_pool, keepalive_timeout=$keepalive_to ==="

    replace_param src/ivf.c "int nprobe = [0-9]*;" "int nprobe = $nprobe;"
    replace_param src/main.c "#define THREAD_POOL [0-9]*" "#define THREAD_POOL $thread_pool"
    replace_param nginx-termux.conf "keepalive_timeout [0-9]*;" "keepalive_timeout $keepalive_to;"

    make clean
    make all

    pkill -9 scoobiii-rinha || true
    pkill -9 nginx || true

    taskset -c 4-7 ./scoobiii-rinha /data/data/com.termux/files/usr/tmp/rinha.sock data/ivf.bin &
    API_PID=$!

    sleep 2
    while [ ! -S /data/data/com.termux/files/usr/tmp/rinha.sock ]; do sleep 1; done

    nginx -c "$PWD/nginx-termux.conf"
    NGINX_PID=$!

    for i in {1..10}; do
        if curl -s -o /dev/null http://localhost:9999/ready; then
            break
        fi
        sleep 1
    done

    wrk_output=$(run_wrk)
    if [ -z "$wrk_output" ]; then
        echo "Falha ao obter resultados do wrk"
        kill $API_PID $NGINX_PID 2>/dev/null || true
        pkill -9 scoobiii-rinha || true
        pkill -9 nginx || true
        sleep 2
        return
    fi
    read p99_ms error_rate <<< "$wrk_output"
    score=$(calc_score "$p99_ms" "$error_rate")

    echo "$nprobe,$thread_pool,$keepalive_to,$p99_ms,$error_rate,$score" >> "$RESULT_FILE"

    kill $API_PID $NGINX_PID 2>/dev/null || true
    pkill -9 scoobiii-rinha || true
    pkill -9 nginx || true
    sleep 2
}

# Valores a testar (ajuste conforme sua paciência)
NPROBE_VALUES=(2 3 4)
THREAD_POOL_VALUES=(8 12 16)
KEEPALIVE_VALUES=(65 120 300)

for n in "${NPROBE_VALUES[@]}"; do
    for t in "${THREAD_POOL_VALUES[@]}"; do
        for k in "${KEEPALIVE_VALUES[@]}"; do
            test_config $n $t $k
        done
    done
done

# Restaurar originais
cp "$BACKUP_DIR/ivf.c" src/
cp "$BACKUP_DIR/main.c" src/
cp "$BACKUP_DIR/nginx-termux.conf" .

make clean
make all

echo "Melhor configuração (maior score):"
sort -t, -k6 -nr "$RESULT_FILE" | head -1
echo "Todos os resultados salvos em $RESULT_FILE"
