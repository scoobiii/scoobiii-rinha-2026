FROM alpine:latest
RUN apk add --no-cache libgcc
COPY server_final /usr/local/bin/
RUN chmod +x /usr/local/bin/server_final
EXPOSE 9999
CMD ["/usr/local/bin/server_final"]
