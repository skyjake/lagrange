# Build from the repository root:
#
#   docker build -f sync/docker/Dockerfile -t lgrsyncd .
#
# Run:
#
#   docker run -d --name lgrsyncd -v lgrsyncd:/data -p 1966:1966 \
#       -e LGRSYNC_ALLOW=<fingerprint> lgrsyncd --host sync.example.com
#
# LGRSYNC_ALLOW is optional. The fingerprint of a client certificate is shown on the
# server's front page (gemini://sync.example.com:1966/).

FROM alpine:3.22 AS build
RUN apk add --no-cache build-base cmake samurai pkgconf \
        openssl-dev libunistring-dev zlib-dev pcre2-dev
WORKDIR /src
COPY sync/CMakeLists.txt sync/
COPY sync/src/ sync/src/
COPY lib/the_Foundation/ lib/the_Foundation/
RUN cmake -S sync -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DTFDN_ENABLE_WARN_ERROR=OFF \
 && cmake --build /build \
 && strip /build/lgrsyncd

FROM alpine:3.22
RUN apk add --no-cache libssl3 libcrypto3 libunistring zlib pcre2 \
 && adduser -D -H -u 1965 lgrsync \
 && mkdir /data \
 && chown lgrsync /data
COPY --from=build /build/lgrsyncd /usr/local/bin/
COPY sync/docker/docker-entrypoint.sh /usr/local/bin/
USER lgrsync
VOLUME /data
EXPOSE 1966
ENTRYPOINT ["docker-entrypoint.sh"]
