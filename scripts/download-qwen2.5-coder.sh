#!/usr/bin/env sh

MODELS_DIR="${MODELS_DIR:-.models}"
MODELS_HOST="${MODELS_HOST:-huggingface.co}"

mkdir -p "$MODELS_DIR"

wget "https://$MODELS_HOST/Qwen/Qwen2.5-Coder-3B-Instruct-GGUF/resolve/main/qwen2.5-coder-3b-instruct-q4_k_m.gguf" \
  -O  "$MODELS_DIR"/qwen2.5-coder-3b-instruct-q4_k_m.gguf
