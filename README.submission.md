# scoobiii-rinha-2026

**Participante:** scoobiii
**Contato:** github.com/scoobiii
**Repositório com código-fonte:** https://github.com/scoobiii/scoobiii-rinha-2026

## Tecnologias

- **Linguagem:** C11
- **Algoritmo:** IVF (Inverted File Index) KNN-5, vetores int16 quantizados
- **Load balancer:** nginx 1.27-alpine via Unix socket
- **Containerização:** Docker multi-stage (builder → indexer → runtime)

## Arquitetura

1 nginx + 2 instâncias da API em C, comunicação via Unix socket.
O índice IVF (1024 centroids, 3M vetores) é gerado no build e carregado via `mmap` compartilhado entre as duas instâncias.
