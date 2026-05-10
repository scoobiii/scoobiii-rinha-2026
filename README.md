# scoobiii-rinha-2026 — C + IVF

Detecção de fraude via busca vetorial KNN-5 com índice IVF em **C puro**.

- **Participante:** scoobiii — [github.com/scoobiii](https://github.com/scoobiii)
- **Contato:** github.com/scoobiii
- **Código-fonte:** https://github.com/scoobiii/scoobiii-rinha-2026

## Stack

| Componente | Detalhe |
|---|---|
| **Linguagem** | C11 — gcc -O3 -march=native |
| **Algoritmo** | IVF (Inverted File Index) KNN-5 |
| **Quantização** | int16 (29 bytes/vetor vs 57 em float32) |
| **I/O** | Unix socket + mmap (MAP_SHARED entre api1/api2) |
| **Load balancer** | nginx 1.27-alpine, round-robin, epoll |
| **Topologia** | 1 LB + 2 instâncias da API |
| **Recursos** | 1.0 CPU / 350 MB total |

## Arquitetura

```
cliente HTTP :9999
      ↓
  nginx (0.15 CPU / 24 MB)
  epoll · unix socket · keepalive
      ↓ round-robin
  api1.sock       api2.sock
      ↓               ↓
  rinha-api       rinha-api
  (0.425 CPU)     (0.425 CPU)
  (163 MB)        (163 MB)
      ↓               ↓
      └──── mmap ──────┘
          ivf.bin
      (MAP_SHARED — OS compartilha
       as páginas entre ambas)
```

## Estrutura do projeto

```
scoobiii-rinha-2026/
├── src/
│   ├── main.c                       # HTTP server + thread pool (4 workers)
│   ├── vectorizer.h                 # 14 dimensões + normalização + mcc_risk
│   ├── ivf.h                        # structs do índice IVF
│   └── ivf.c                        # mmap load + KNN-5 + distância² int16
├── tools/
│   └── preprocess.c                 # build-time: references.json.gz → ivf.bin
├── resources/                       # coloque references.json.gz aqui (git-ignored)
├── Dockerfile                       # 3 stages: builder → indexer → runtime
├── docker-compose.yml               # dev local  (build: .)
├── docker-compose.submission.yml    # submissão  (image: ghcr.io/...)
├── nginx.conf
├── Makefile
└── README.md
```

## 14 Dimensões (REGRAS_DE_DETECCAO.md)

| dim | campo | fórmula |
|---|---|---|
| 0 | amount | clamp(amount / 10000) |
| 1 | installments | clamp(installments / 12) |
| 2 | amount_vs_avg | clamp((amount / avg_amount) / 10) |
| 3 | hour_of_day | hour(requested_at) / 23 |
| 4 | day_of_week | dow(requested_at) / 6 |
| 5 | minutes_since_last_tx | clamp(minutos / 1440) ou **-1** |
| 6 | km_from_last_tx | clamp(km / 1000) ou **-1** |
| 7 | km_from_home | clamp(km / 1000) |
| 8 | tx_count_24h | clamp(count / 20) |
| 9 | is_online | 0 ou 1 |
| 10 | card_present | 0 ou 1 |
| 11 | unknown_merchant | 1 se desconhecido, 0 se conhecido |
| 12 | mcc_risk | lookup mcc_risk.json (hardcoded no build) |
| 13 | merchant_avg_amount | clamp(avg / 10000) |

## Índice IVF

- **Build** (Dockerfile Stage 2): K-means k=1024, 20 iterações → `ivf.bin`
- **Query**: nprobe=32 células, max-heap KNN-5
- **Complexidade**: O(32 × N/1024) ≈ 93k comparações/request

## Scoring

```c
fraud_score = fraudes_entre_5_vizinhos / 5
approved    = fraud_score < 0.6
```

## Rodar LOCAL (dev)

```bash
# 1. Obter dataset
git clone https://github.com/zanfranceschi/rinha-de-backend-2026
cp -r rinha-de-backend-2026/resources ./

# 2. Build + sobe (~5-10 min pelo K-means no Stage 2)
docker compose up --build

# 3. Testar
curl http://localhost:9999/ready

curl -X POST http://localhost:9999/fraud-score \
  -H "Content-Type: application/json" \
  -d '{
    "id": "tx-test",
    "transaction": {"amount": 384.88, "installments": 3,
                    "requested_at": "2026-03-11T20:23:35Z"},
    "customer": {"avg_amount": 769.76, "tx_count_24h": 3,
                 "known_merchants": ["MERC-001"]},
    "merchant": {"id": "MERC-001", "mcc": "5912", "avg_amount": 298.95},
    "terminal": {"is_online": false, "card_present": true, "km_from_home": 13.7},
    "last_transaction": null
  }'
```

## Publicar imagem (submissão)

```bash
docker build -t ghcr.io/scoobiii/scoobiii-rinha-2026:latest .
docker push ghcr.io/scoobiii/scoobiii-rinha-2026:latest
```

## Recursos (limites Rinha)

| Serviço | CPU | Memória |
|---|---|---|
| nginx | 0.15 | 24 MB |
| api1 | 0.425 | 163 MB |
| api2 | 0.425 | 163 MB |
| **Total** | **1.00** | **350 MB ✓** |

## Roadmap — próximos sprints

| Técnica | Ganho esperado | Status |
|---|---|---|
| Thread pool fixo (sem pthread_create/req) | −50µs | ✅ |
| IVF nprobe=32 / 1024 clusters | −0.3ms | ✅ |
| int16 quantizado + dist² sem sqrt | 4× cache hit | ✅ |
| Unix socket (sem TCP overhead) | −0.1ms | ✅ |
| AVX2 SIMD dist² (padding 16D) | −0.15ms | 🔜 |
| io_uring (substituir epoll+threads) | −0.2ms p99 | 🔜 |

**Meta: p99 < 1.10ms**
