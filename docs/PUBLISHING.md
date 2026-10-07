# Publishing the source

The edited Qt donor is an ordinary directory without nested Git history.
Core and the Desktop behavior reference are fetched by `make init` into ignored
`context/` and excluded from source exports. Preserve licenses and attribution. Private
accounts, build products and the project-local Nix store are ignored under data/.

Before every push:

```sh
make test
git status --short
git diff --cached --check
git ls-files --stage | awk '$1 == "160000" { print }'
git verify-commit HEAD
```

The gitlink check should print nothing. The source audit scans non-ignored files
for local home paths, private markers, common tokens, private key payloads,
external symlinks, ELF binaries and GitHub's single-file size limit. IPv4
locations can be reviewed using `./nix/develop.sh --command python3
nix/audit-source.py --ips`. The retained addresses belong to donor endpoints,
fixtures or examples; some matches are version numbers. The MSVC Spectre option
is also donor source. These checks cannot prove that every secret is absent.

Git uses the requested global identity and signing key. The configured email
appears publicly in commit metadata. Before subsequent commits, review that
identity with `git var GIT_AUTHOR_IDENT`. Initial source preparation does not
create a GitHub repository or push to one.

After creating an empty GitHub repository, set its URL and push:

```sh
git remote add origin https://github.com/YOUR_ACCOUNT/YOUR_REPOSITORY.git
git push -u origin main
```

For a separate source copy, run `nix/export-public-source.py` in the development
shell with a new destination outside this project. It excludes runtime state and
Git metadata and supports materialized source directories.
