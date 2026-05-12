#!/bin/bash
set -e

echo "Preparando branch submission..."

# Salva mudanças atuais (caso haja)
git stash push -m "temp before submission" || true

# Garante que os arquivos necessários estão presentes e corretos
git checkout main

# Aplica tuning final (caso ainda não esteja)
sed -i 's/int nprobe = [0-9]*;/int nprobe = 2;/' src/ivf.c
sed -i 's/#define THREAD_POOL [0-9]*/#define THREAD_POOL 16/' src/main.c
sed -i 's/keepalive_timeout [0-9]*;/keepalive_timeout 120;/' nginx.conf

# Commit na main (se houver mudanças)
git add src/ivf.c src/main.c nginx.conf
git commit -m "perf: final tuned config (nprobe=2, THREAD_POOL=16, keepalive=120)" || echo "Nada a commitar"

# Força a criação/atualização da branch submission
git checkout -B submission

# Remove arquivos desnecessários para submissão (opcional, mas mantém clean)
rm -f scoobiii-rinha preprocess *.o

# Garante que os arquivos essenciais estão na raiz
git add docker-compose.yml nginx.conf info.json .gitignore Makefile Dockerfile src/ tools/
git commit -m "submission: ready for Rinha 2026" || echo "Branch submission já atualizada"

echo "Branch submission pronta. Agora faça push com:"
echo "  git push origin submission --force"
