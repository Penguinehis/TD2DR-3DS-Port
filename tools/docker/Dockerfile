# Asset tools image: ffmpeg + Python for tools/export_audio.py
#   docker build -t sonic3ds-tools tools/docker
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends ffmpeg python3 && rm -rf /var/lib/apt/lists/*
