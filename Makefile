SHELL := /bin/bash
.DEFAULT_GOAL := build
.PHONY: init build test release release-all clean

init:
	bash nix/init-context.sh
	./nix/install-nix.sh
	./nix/build-deps.sh
	./nix/develop.sh --command true

build:
	./nix/develop.sh --command bash -c 'bash nix/build-rpc.sh && bash nix/build-tdesktop.sh'

test:
	./nix/develop.sh --command python3 nix/test-bump-version.py
	for script in nix/*.sh docker/*.sh; do bash -n "$$script" || exit; done
	./nix/develop.sh --command python3 nix/audit-source.py

release:
	bash nix/release.sh

release-all:
	bash nix/release.sh all

# Remove disposable exports only; preserve Ninja records and compiler caches.
clean:
	rm -rf -- dist publish-output
