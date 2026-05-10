#!/bin/bash

mkdir -p src

touch Dockerfile \
      Makefile \
      README.md \
      docker-compose.yml \
      info.json \
      nginx.conf \
      src/ivf.c \
      src/ivf.h \
      src/main.c \
      src/preprocess.c \
      src/vectorizer.h

tree .
