# Dockerfile para Rinha 2026 - Detecção de Fraude
FROM ubuntu:24.04

# Instala dependências
RUN apt-get update && apt-get install -y \
    gcc \
    make \
    libc6-dev \
    && rm -rf /var/lib/apt/lists/*

# Cria diretório da aplicação
WORKDIR /app

# Copia código fonte
COPY src/ ./src/
COPY Makefile .
COPY start.sh .
COPY nginx.conf .
COPY info.json .

# Compila a aplicação
RUN make clean && make

# Expõe a porta
EXPOSE 9999

# Script de entrada
CMD ["./start.sh"]
