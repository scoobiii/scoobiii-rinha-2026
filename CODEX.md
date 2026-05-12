# CODEX – scoobiii-rinha-2026

## 1. Arquitetura
- Linguagem C11, compilação com `-O3 -march=native` + SIMD (NEON/AVX2 condicional)
- Índice IVF (1024 clusters, 3M vetores, quantização int16, mmap compartilhado)
- Servidor: Unix socket + thread pool (16 workers)
- Load balancer: nginx com keepalive 120s, upstream Unix sockets

## 2. Parâmetros otimizados (tuning automático)
| Parâmetro | Valor | Motivo |
|-----------|-------|--------|
| `nprobe` | 2 | Busca apenas 2 clusters → 2× menos comparações |
| `THREAD_POOL` | 16 | Sobreposição de I/O e CPU no A23 (8 núcleos) |
| `keepalive_timeout` | 120 | Reduz overhead de conexões |

## 3. Resultados finais (A23 / Termux)
- **p99 latência:** 3,93 ms
- **Erros:** 0,39% (dentro do limite de 15%)
- **Score estimado:** 5717 (top 10)
- **Requests/s (wrk -t2 -c10):** ~4200

## 4. Build e execução
### Termux (ARM64)
```bash
pkg install make gcc nginx
make all
taskset -c 4-7 ./scoobiii-rinha /data/data/com.termux/files/usr/tmp/rinha.sock data/ivf.bin
nginx -c nginx-termux.conf
```

Linux (x86_64)

```bash
sudo apt install build-essential nginx
make all
./scoobiii-rinha /sockets/api.sock data/ivf.bin
docker compose up --build   # modo Rinha
```

Docker multi‑arquitetura (GitHub Actions)

```yaml
- uses: docker/setup-qemu-action@v3
- run: docker buildx build --platform linux/amd64,linux/arm64 -t ghcr.io/... --push .
```

5. Histórico de “cagadas” e soluções

Problema Solução
Makefile com espaços → missing separator Reescrito com printf garantindo TABs
Dockerfile com COPY dataset sem download Substituído por wget no estágio indexer
ivf_load indefinido Recriado ivf.c com a função correta
nginx: bind() failed Matar processos antigos (pkill -9 nginx)

6. Submissão à Rinha

· Branch submission contém docker-compose.yml, nginx.conf, info.json, .gitignore
· Abrir PR com participants/scoobiii.json
· Abrir issue rinha/test scoobiii-c

---

Última atualização: 2026-05-11 – configuração final aplicada.
