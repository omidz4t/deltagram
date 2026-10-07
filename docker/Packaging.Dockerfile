FROM debian:13-slim@sha256:a99cfc517144bc59b1978475ec53b46ecabec7e43635402ee5b77cc54cd1b20a
RUN apt-get update && apt-get install -y --no-install-recommends \
    dpkg-dev rpm zstd tar gzip libarchive-tools ca-certificates \
    && rm -rf /var/lib/apt/lists/*
COPY make-packages.sh /builder/make-packages.sh
ENTRYPOINT ["/bin/bash", "/builder/make-packages.sh"]
