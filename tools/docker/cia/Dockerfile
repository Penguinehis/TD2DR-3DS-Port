# CIA packaging tools: devkitARM + makerom (Project_CTR) + bannertool (carstene1ns fork,
# built from source: the prebuilt Linux binaries need a newer glibc than this image has).
#   docker build -t sonic3ds-cia tools/docker/cia
FROM devkitpro/devkitarm
RUN apt-get update && apt-get install -y --no-install-recommends wget unzip ca-certificates build-essential cmake \
 && rm -rf /var/lib/apt/lists/*
RUN wget -q https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v0.18.4/makerom-v0.18.4-ubuntu_x86_64.zip -O /tmp/makerom.zip \
 && unzip -o /tmp/makerom.zip -d /usr/local/bin && chmod +x /usr/local/bin/makerom && rm -f /tmp/makerom.zip
RUN wget -q https://github.com/carstene1ns/3ds-bannertool/releases/download/1.2.3/bannertool-1.2.3.tar.gz -O /tmp/bt.tar.gz \
 && mkdir -p /tmp/bt && tar -xzf /tmp/bt.tar.gz -C /tmp/bt --strip-components=1 \
 && cmake -S /tmp/bt -B /tmp/bt/build -DCMAKE_BUILD_TYPE=Release >/dev/null \
 && cmake --build /tmp/bt/build -j8 >/dev/null \
 && find /tmp/bt/build -type f -name bannertool -perm -u+x -exec cp {} /usr/local/bin/bannertool \; \
 && rm -rf /tmp/bt /tmp/bt.tar.gz
