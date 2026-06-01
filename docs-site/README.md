# Volt Public Docs

This directory contains the Mintlify source for Volt's public-facing documentation.

## Local preview

Install the Mintlify CLI using the current Mintlify instructions:

```sh
npm i -g mint
```

Then run:

```sh
cd docs-site
mint dev
```

## Correctness check

From the repository root:

```sh
python3 scripts/generate-python-api-docs.py --check
python3 scripts/check-docs-site.py
```

The generator check validates that `docs-site/api/python/` matches the public Python
source. The docs-site check validates the Mintlify navigation, required first-read pages,
frontmatter, workflow snippets, and coverage of the public names exported by
`python/volt/__init__.py`.
