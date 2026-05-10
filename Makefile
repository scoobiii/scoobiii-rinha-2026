services:

  nginx:
    image: nginx:1.27-alpine
    volumes:
      - ./nginx.conf:/etc/nginx/nginx.conf:ro
      - sockets:/sockets
    ports:
      - "9999:9999"
    depends_on:
      - api1
      - api2
    networks:
      - backend
    deploy:
      resources:
        limits:
          cpus: "0.15"
          memory: "24MB"

  api1:
    image: ghcr.io/scoobiii/scoobiii-rinha-2026:latest
    command: ["/usr/local/bin/rinha-api", "/sockets/api1.sock", "/data/ivf.bin"]
    volumes:
      - sockets:/sockets
    networks:
      - backend
    deploy:
      resources:
        limits:
          cpus: "0.425"
          memory: "163MB"

  api2:
    image: ghcr.io/scoobiii/scoobiii-rinha-2026:latest
    command: ["/usr/local/bin/rinha-api", "/sockets/api2.sock", "/data/ivf.bin"]
    volumes:
      - sockets:/sockets
    networks:
      - backend
    deploy:
      resources:
        limits:
          cpus: "0.425"
          memory: "163MB"

volumes:
  sockets:

networks:
  backend:
    driver: bridge

# Total: 0.15 + 0.425 + 0.425 = 1.00 CPU
#        24   + 163   + 163   = 350 MB ✓
