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

## Setup — primeira vez (Google Cloud Shell ou máquina local)

```bash
# 1. Clonar este repositório
git clone https://github.com/scoobiii/scoobiii-rinha-2026.git
cd scoobiii-rinha-2026

# 2. Obter o dataset oficial (references.json.gz ~94 MB)
git clone https://github.com/zanfranceschi/rinha-de-backend-2026.git
cp -r rinha-de-backend-2026/resources ./
# confirmar que existe:
ls resources/references.json.gz

# 3. Build e subir (primeira vez leva ~5-10 min pelo K-means)
docker compose up --build

# 4. Em outro terminal — testar
curl http://localhost:9999/ready
```

> **Atenção:** `docker compose up` (sem `--build`) tenta puxar a imagem remota do ghcr.io
> e falha se ela não existir. **Sempre use `--build` na primeira vez** ou após mudanças no código.

## Testar o endpoint

```bash
curl -s -X POST http://localhost:9999/fraud-score \
  -H "Content-Type: application/json" \
  -d '{
    "id": "tx-test",
    "transaction": {
      "amount": 384.88,
      "installments": 3,
      "requested_at": "2026-03-11T20:23:35Z"
    },
    "customer": {
      "avg_amount": 769.76,
      "tx_count_24h": 3,
      "known_merchants": ["MERC-001"]
    },
    "merchant": {
      "id": "MERC-001",
      "mcc": "5912",
      "avg_amount": 298.95
    },
    "terminal": {
      "is_online": false,
      "card_present": true,
      "km_from_home": 13.7
    },
    "last_transaction": null
  }'
# Esperado: {"approved":true,"fraud_score":0.0000}
```

## Publicar imagem para submissão

```bash
# Autenticar no GitHub Container Registry
echo $GITHUB_TOKEN | docker login ghcr.io -u scoobiii --password-stdin

# Build e push
docker build -t ghcr.io/scoobiii/scoobiii-rinha-2026:latest .
docker push ghcr.io/scoobiii/scoobiii-rinha-2026:latest

# Tornar a imagem pública em:
# github.com/scoobiii → Packages → scoobiii-rinha-2026 → Package settings → Make public
```

## Submissão na Rinha (PR)

```bash
# No seu fork de zanfranceschi/rinha-de-backend-2026,
# branch submission, pasta participants/scoobiii-c/:
#   - docker-compose.yml  (conteúdo de docker-compose.submission.yml)
#   - nginx.conf
#   - README.md           (conteúdo de README.submission.md)
```

## 14 Dimensões (REGRAS_DE_DETECCAO.md)

| dim | campo | fórmula |
|---|---|---|
| 0 | amount | clamp(amount / 10000) |
| 1 | installments | clamp(installments / 12) |
| 2 | amount_vs_avg | clamp((amount / avg_amount) / 10) |
| 3 | hour_of_day | hour(requested_at) / 23 |
| 4 | day_of_week | dow(requested_at) / 6 |
| 5 | minutes_since_last_tx | clamp(min / 1440) ou **-1** |
| 6 | km_from_last_tx | clamp(km / 1000) ou **-1** |
| 7 | km_from_home | clamp(km / 1000) |
| 8 | tx_count_24h | clamp(count / 20) |
| 9 | is_online | 0 ou 1 |
| 10 | card_present | 0 ou 1 |
| 11 | unknown_merchant | 1 desconhecido / 0 conhecido |
| 12 | mcc_risk | lookup mcc_risk.json (hardcoded) |
| 13 | merchant_avg_amount | clamp(avg / 10000) |

## Recursos (limites Rinha)

| Serviço | CPU | Memória |
|---|---|---|
| nginx | 0.15 | 24 MB |
| api1 | 0.425 | 163 MB |
| api2 | 0.425 | 163 MB |
| **Total** | **1.00** | **350 MB ✓** |

## Roadmap

| Técnica | Ganho | Status |
|---|---|---|
| Thread pool fixo | −50µs | ✅ |
| IVF nprobe=32 / k=1024 | −0.3ms | ✅ |
| int16 + dist² sem sqrt | 4× cache | ✅ |
| Unix socket | −0.1ms | ✅ |
| AVX2 SIMD dist² | −0.15ms | 🔜 |
| io_uring | −0.2ms p99 | 🔜 |

**Meta: p99 < 1.10ms**
