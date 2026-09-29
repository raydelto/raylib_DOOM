# The ps2dev toolchain with raylib4PlayStation2 built into
# $PS2SDK/ports, for packaging/ps2/build.sh:
#
#   docker build -t raylibdoom-ps2 packaging/ps2
#   docker run --rm -v "$PWD:/src" -w /src raylibdoom-ps2 packaging/ps2/build.sh
#
# ps2dev/ps2dev:latest as of 2026-09-29, pinned so the toolchain
# only changes when this line does.
ARG PS2DEV_IMAGE=ps2dev/ps2dev:latest@sha256:6e7935b0c9b0e1b89e1dd81293ced20723af16937be22918072499862deda156
FROM ${PS2DEV_IMAGE}

# The image is Alpine: raylib's install needs GNU cp (--update).
RUN apk add --no-cache bash coreutils git make

COPY build-deps.sh raylib4Consoles_6.0-ps2.patch /opt/raylibdoom-ps2/
RUN /opt/raylibdoom-ps2/build-deps.sh /tmp/raylib4ps2 && rm -rf /tmp/raylib4ps2
