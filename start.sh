#!/bin/bash

# 1. Limpeza de sockets antigos
rm -f /data/data/com.termux/files/usr/tmp/api*.sock
rm -f nginx.pid

echo "🚀 Subindo Instâncias da API..."
./rinha-api /data/data/com.termux/files/usr/tmp/api1.sock data/ivf.bin &
./rinha-api /data/data/com.termux/files/usr/tmp/api2.sock data/ivf.bin &

sleep 2 # Espera os sockets serem criados

echo "🌐 Subindo Nginx..."
nginx -c $(pwd)/nginx-termux.conf -g "daemon off;"
