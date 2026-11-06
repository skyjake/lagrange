# OpenSSL feature options for the dependency build scripts. Source this file and
# pass "${OPENSSL_OPTS[@]}" to Configure.
#
# Gemini needs TLS 1.2/1.3 with AES-GCM or ChaCha20-Poly1305 (see main.c), so
# QUIC, the legacy provider, and the legacy ciphers are left out.

OPENSSL_OPTS=(
    no-quic
    no-legacy
    no-aria
    no-bf
    no-camellia
    no-cast
    no-des
    no-idea
    no-rc2
    no-rc4
    no-seed
    no-sm4
)
