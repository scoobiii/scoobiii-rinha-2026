# scoobiii-rinha-2026 — C + IVF

Detecção de fraude em transações via busca vetorial com índice IVF em **C puro**.

## Stack

| Componente | Detalhe |
|---|---|
| **Linguagem** | C11 (gcc -O3 -march=native) |
| **Algoritmo** | IVF (Inverted File Index) KNN-5 |
| **Vetores** | 14 dimensões, int16 quantizado |
| **Load balancer** | nginx 1.27-alpine + Unix socket |
| **Topologia** | 1 LB + 2 instâncias da API |
| **Recursos** | 1.0 CPU / 350MB total |

## Arquitetura

```
cliente
  ↓ :9999
nginx (Unix socket round-robin)
  ↓          ↓
api1.sock  api2.sock
  ↓          ↓
[rinha-api] (mmap IVF index)
```

## Como funciona

### Vetorização (14 dimensões)

Conforme `REGRAS_DE_DETECCAO.md`:

| dim | campo | fórmula |
|---|---|---|
| 0 | amount | clamp(amount / 10000) |
| 1 | installments | clamp(installments / 12) |
| 2 | amount_vs_avg | clamp((amount/avg_amount)/10) |
| 3 | hour_of_day | hour(requested_at) / 23 |
| 4 | day_of_week | dow(requested_at) / 6 |
| 5 | minutes_since_last_tx | clamp(minutes/1440) ou -1 |
| 6 | km_from_last_tx | clamp(km/1000) ou -1 |
| 7 | km_from_home | clamp(km/1000) |
| 8 | tx_count_24h | clamp(count/20) |
| 9 | is_online | 0 ou 1 |
| 10 | card_present | 0 ou 1 |
| 11 | unknown_merchant | 1 se desconhecido, 0 se conhecido |
| 12 | mcc_risk | tabela mcc_risk.json |
| 13 | merchant_avg_amount | clamp(avg/10000) |

### Índice IVF

- **Build** (Dockerfile Stage 2): `references.json.gz` → `ivf.bin`
  - K-means com k=1024 centroids, 20 iterações
  - Cada vetor quantizado para int16 (29 bytes vs 57 bytes em float32)
- **Query**: nprobe=32 células, heap max-K para KNN-5
- **Complexidade**: O(nprobe × N/k) ≈ O(32 × 2929) ≈ 93k comparações

### Scoring

```
fraud_score = fraudes_entre_5 / 5
approved    = fraud_score < 0.6
```

## Build & Run

```bash
# Build + preprocess (leva ~5-10min pela primeira vez)
docker compose up --build

# Testar
curl http://localhost:9999/ready
curl -X POST http://localhost:9999/fraud-score \
  -H "Content-Type: application/json" \
  -d '{
    "id": "tx-test",
    "transaction": {"amount": 384.88, "installments": 3, "requested_at": "2026-03-11T20:23:35Z"},
    "customer": {"avg_amount": 769.76, "tx_count_24h": 3, "known_merchants": ["MERC-001"]},
    "merchant": {"id": "MERC-001", "mcc": "5912", "avg_amount": 298.95},
    "terminal": {"is_online": false, "card_present": true, "km_from_home": 13.7},
    "last_transaction": null
  }'
```

## Submissão

Branch `submission` contém apenas:
- `docker-compose.yml`
- `nginx.conf`
- `info.json`

Código-fonte na branch `main`.

## Análise vs top performers

| # | Submissão | p99 | Score | Algoritmo |
|---|---|---|---|---|
| 1 | jairoblatt-rust | 1.14ms | 5941 | Rust + ? |
| 2 | andrade-cpp-ivf | 1.20ms | 5920 | C + IVF + int16 |
| **?** | **scoobiii-c** | **~1.5ms?** | **~5800?** | **C + IVF + int16** |

Para chegar no top-5 precisamos atingir p99 < 1.5ms com 0% fail.

## Participantes

- scoobiii (github.com/scoobiii)
