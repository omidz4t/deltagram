FROM debian:13-slim@sha256:a99cfc517144bc59b1978475ec53b46ecabec7e43635402ee5b77cc54cd1b20a

# Tools and staging are downloaded into the container's temporary build mount.
# The existing release and Nix toolchain are read-only inputs, never image layers.
COPY build-bundle.sh package.py launcher.c AppRun /builder/
ENTRYPOINT ["/bin/bash", "/builder/build-bundle.sh"]
