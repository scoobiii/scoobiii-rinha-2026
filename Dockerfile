FROM alpine:latest

# Instala dependências
RUN apk add --no-cache libgcc libc6-compat

# Copia o binário compilado
COPY server_final /usr/local/bin/server_final
COPY cJSON.c /usr/local/lib/
COPY cJSON.h /usr/local/include/

# Permissão de execução
RUN chmod +x /usr/local/bin/server_final

# Expõe a porta
EXPOSE 9999

# Comando para rodar
CMD ["/usr/local/bin/server_final"]
