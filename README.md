# 🚀 Rinha Backend 2026 - Implementação em C (scoobiii-c)

[![Rinha Status](https://img.shields.io/badge/Rinha-2026-blue)](https://github.com/zanfranceschi/rinha-de-backend-2026/pull/1066)
[![License](https://img.shields.io/badge/License-MIT-green)](LICENSE)
[![C](https://img.shields.io/badge/C-99-blue)](https://gcc.gnu.org/)
[![Platform](https://img.shields.io/badge/Platform-AMD64%20%7C%20ARM64-lightgrey)](https://github.com/scoobiii)

## 📋 Índice

- [Sobre](#-sobre)
- [Arquitetura](#-arquitetura)
- [Requisitos](#-requisitos)
- [Quick Start](#-quick-start)
- [Instalação por Ambiente](#-instalação-por-ambiente)
  - [Mobile (Termux/Android)](#mobile-termuxandroid)
  - [Desktop (Linux/Ubuntu)](#desktop-linuxubuntu)
  - [Google Cloud VM](#google-cloud-vm)
  - [Docker (Produção)](#docker-produção)
- [API Endpoints](#-api-endpoints)
- [Benchmark](#-benchmark)
- [Estrutura do Projeto](#-estrutura-do-projeto)
- [Submissão Rinha](#-submissão-rinha)
- [Solução de Problemas](#-solução-de-problemas)

---

## 🎯 Sobre

Este é o backend da Rinha de Backend 2026, implementado em **C puro** com:

- ✅ **Int8 Quantization** - Memória eficiente (~45MB para 3M vetores)
- ✅ **KNN com k=5** - Busca dos 5 vizinhos mais próximos
- ✅ **Threshold 0.6** - Limiar de decisão fixo
- ✅ **14 dimensões** - Conforme especificação da Rinha
- ✅ **Load Balancer** - Nginx + 2 instâncias da API

### 📊 Performance

| Métrica | Valor | Limite Rinha |
|---------|-------|--------------|
| **Memória** | ~45 MB | 350 MB ✅ |
| **Latência** | ~14-30 ms | 2000 ms ✅ |
| **Throughput** | ~120 req/s | - |
| **CPU** | 30-40% (1 core) | 1 core ✅ |

---

## 🏗️ Arquitetura

```

┌─────────────────────────────────────────────────────────┐
│                      CLIENTE (Porta 9999)                │
└─────────────────────────────────────────────────────────┘
│
▼
┌─────────────────────────────────────────────────────────┐
│              LOAD BALANCER (Nginx - Round Robin)        │
└─────────────────────────────────────────────────────────┘
│                       │
▼                       ▼
┌─────────────────┐     ┌─────────────────┐
│     API 1       │     │     API 2       │
│   Porta 9998    │     │   Porta 9997    │
│  C + Int8 +     │     │  C + Int8 +     │
│  Brute Force    │     │  Brute Force    │
└─────────────────┘     └─────────────────┘

```

---

## 📦 Requisitos

| Ambiente | Requisitos |
|----------|------------|
| **Mobile** | Termux, gcc, make, git |
| **Desktop** | GCC, make, git, libc6-dev |
| **GCloud** | Ubuntu 22.04+, GCC, Docker (opcional) |
| **Docker** | Docker Engine 20.10+, Docker Compose |

---

## ⚡ Quick Start

```bash
# Clone
git clone https://github.com/scoobiii/scoobiii-rinha-2026
cd scoobiii-rinha-2026

# Compila
gcc -O3 -pthread -lm -o server_final src/server_final.c src/cJSON.c

# Executa
./server_final

# Testa
curl -X POST http://localhost:9999/fraud-score \
  -H 'Content-Type: application/json' \
  -d '{"transaction":{"amount":100}}'
```

Resposta esperada:

```json
{"approved":true,"fraud_score":0.00}
```

---

📱 Instalação por Ambiente

Mobile (Termux/Android)

```bash
# Instala dependências
pkg update && pkg upgrade -y
pkg install gcc make git

# Clone e compile
git clone https://github.com/scoobiii/scoobiii-rinha-2026
cd scoobiii-rinha-2026
gcc -O3 -pthread -lm -o server_final src/server_final.c src/cJSON.c

# Execute
./server_final
```

⚠️ O binário gerado será ARM64. Para produção na Rinha, use cross-compilação.

Desktop (Linux/Ubuntu)

```bash
# Instala dependências
sudo apt update
sudo apt install -y build-essential git

# Clone e compile
git clone https://github.com/scoobiii/scoobiii-rinha-2026
cd scoobiii-rinha-2026
gcc -O3 -pthread -lm -o server_final src/server_final.c src/cJSON.c

# Execute
./server_final
```

Google Cloud VM

```bash
# SSH na VM
gcloud compute ssh sua-vm --zone=us-central1-a

# Instala dependências
sudo apt update && sudo apt install -y build-essential git

# Clone e compile
git clone https://github.com/scoobiii/scoobiii-rinha-2026
cd scoobiii-rinha-2026
gcc -O3 -pthread -lm -o server_final src/server_final.c src/cJSON.c

# Roda em background
nohup ./server_final > server.log 2>&1 &

# Testa
curl http://localhost:9999/ready
```

Docker (Produção)

```bash
# Build da imagem
docker build -t scoobiii-rinha .

# Executa
docker run -d --name rinha-api -p 9999:9999 --memory=350m --cpus=1 scoobiii-rinha

# Com Docker Compose (Load Balancer + 2 APIs)
docker-compose up -d

# Verifica logs
docker-compose logs -f
```

---

🔌 API Endpoints

GET /ready

Verifica se a API está pronta.

```bash
curl http://localhost:9999/ready
# Resposta: HTTP 200 OK
```

POST /fraud-score

Detecta fraude em transação.

Payload completo:

```json
{
  "id": "tx-3576980410",
  "transaction": {
    "amount": 384.88,
    "installments": 3,
    "requested_at": "2026-03-11T20:23:35Z"
  },
  "customer": {
    "avg_amount": 769.76,
    "tx_count_24h": 3,
    "known_merchants": ["MERC-009", "MERC-001"]
  },
  "merchant": {
    "id": "MERC-001",
    "mcc": "5912",
    "avg_amount": 298.95
  },
  "terminal": {
    "is_online": false,
    "card_present": true,
    "km_from_home": 13.7090520965
  },
  "last_transaction": null
}
```

Resposta:

```json
{
  "approved": true,
  "fraud_score": 0.00
}
```

---

📊 Benchmark

Resultados (3M vetores, AMD64)

Métrica Valor
Build time ~0.37s (1M vetores)
Memória 45 MB
Latência média 14-30 ms
Latência p99 ~45 ms
Throughput 120 req/s
CPU (1 core) 30-40%

Teste de carga

```bash
# 100 requisições
time for i in {1..100}; do
  curl -s -X POST http://localhost:9999/fraud-score \
    -H 'Content-Type: application/json' \
    -d '{"transaction":{"amount":100}}' > /dev/null
done
# Resultado: ~3s (30ms/req)
```

---

📁 Estrutura do Projeto

```
scoobiii-rinha-2026/
├── src/
│   ├── server_final.c      # Código principal
│   ├── cJSON.c             # Parser JSON
│   └── cJSON.h             # Header JSON
├── submission/              # Arquivos de deploy
│   ├── docker-compose.yml
│   └── nginx.conf
├── Dockerfile               # Build da imagem
├── info.json                # Metadados
├── scoobiii.json           # Participação Rinha
└── README.md               # Este arquivo
```

---

🚀 Submissão Rinha

Pull Request

🔗 https://github.com/zanfranceschi/rinha-de-backend-2026/pull/1066

Branches

Branch Conteúdo
main Código-fonte
submission Docker, nginx, binário

Status

· ✅ Smoke test passou
· ✅ Validação ok
· ⏳ Aguardando revisão humana

---

🐛 Solução de Problemas

Erro: "Address already in use"

```bash
pkill -f server_final
fuser -k 9999/tcp
```

Erro: "binário ARM64 vs AMD64"

```bash
file server_final
# Deve mostrar: ELF 64-bit LSB executable, x86-64
```

Erro: "undefined reference to sqrtf"

```bash
gcc -O3 -pthread -o server_final server_final.c cJSON.c -lm
```

Erro: "Permission denied"

```bash
chmod +x server_final
```

---

📄 Licença

MIT © scoobiii

---

🙏 Agradecimentos

· zanfranceschi pela Rinha de Backend
· Comunidade Rinha 2026

---

🏆 Submissão oficial: https://github.com/zanfranceschi/rinha-de-backend-2026/pull/1066
