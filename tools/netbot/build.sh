#!/bin/sh
# Builds tools/netbot/bot (Linux) with the server's ENet. Run from the repo root in Docker:
#   docker run --rm -v "<repo>:/project" -v "<betterserver>/enet:/enet" -w /project gcc:13 sh tools/netbot/build.sh
set -e
gcc -O1 -o tools/netbot/bot tools/netbot/bot.c /enet/callbacks.c /enet/compress.c /enet/host.c /enet/list.c \
    /enet/packet.c /enet/peer.c /enet/protocol.c /enet/unix.c -I/enet/include -DHAS_SOCKLEN_T=1 -lm
echo built tools/netbot/bot
