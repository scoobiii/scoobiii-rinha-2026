#!/bin/bash

# 1. Limpeza de ambiente e liberação de RAM
echo "🧹 Limpando processos e liberando cache..."
pkill -9 scoobiii-rinha
pkill -9 nginx
sleep 1
rm -f /data/data/com.termux/files/usr/tmp/rinha.sock

# 2. Iniciar o motor Scoobiii
echo "⚙️ Iniciando motor de busca (nprobe=3)..."
./scoobiii-rinha &
API_PID=$! 
sleep 2

# 3. OTIMIZAÇÃO DE HARDWARE (Root)
# Prioridade de tempo real no kernel
echo "🚀 Elevando prioridade para Nice -20..."
renice -n -20 -p $API_PID 2>/dev/null

# Fixar nos núcleos Cortex-A73 (4-7) para evitar jitter
echo "🎯 Fixando afinidade de CPU nos núcleos de performance (4-7)..."
taskset -pc 4-7 $API_PID 2>/dev/null

# 4. Iniciar o Nginx
echo "🌐 Subindo proxy reverso Nginx..."
nginx -c $(pwd)/nginx-termux.conf
NGINX_PID=$(pgrep nginx | head -n 1)
renice -n -15 -p $NGINX_PID 2>/dev/null
sleep 1

# 5. ROTINA DE WARM-UP (Carregar mmap na RAM)
echo "🔥 Aquecendo RAM e travando clock da CPU (10s)..."
wrk -t2 -c2 -d10s -H "Connection: keep-alive" http://localhost:9999/ > /dev/null

echo "✅ Sistema em estado de prontidão. Estabilizando..."
sleep 3

# 6. BENCHMARK OFICIAL (Apontando para ~/rinha_test.lua)
echo "📊 Rodando Benchmark Final (2 Threads / 2 Conexões)..."
wrk -t2 -c2 -d30s --timeout 10s -H "Connection: keep-alive" -s ~/rinha_test.lua --latency http://localhost:9999/

echo "🏁 Teste concluído com sucesso."
