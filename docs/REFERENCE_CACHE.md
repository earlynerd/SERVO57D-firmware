# Local Reference Cache

The project keeps original third-party PDFs under ignored `reference/local/`
and generates an ignored, searchable cache under `reference/cache/`. The cache
reduces repeated PDF rendering while preserving a direct path back to the
original document and PDF page.

The tracked `reference/sources.json` catalog gives each important document a
stable ID and records its title, version, relative local path, page count, and
SHA-256 digest. A source is not extracted or rendered unless its digest and
page count match the catalog.

## What the cache contains

Each source receives:

- `manifest.json`: source provenance, extractor version, page count, and page
  file index.
- `pages/NNNN.txt`: UTF-8 text for one PDF page plus source ID, version, PDF
  page, digest, and extraction metadata.
- `renders/NNNN-DDDdpi.png`: on-demand page images accompanied by JSON
  provenance sidecars.

Text is the fast discovery layer. The original PDF remains authoritative for
schematics, diagrams, tables, footnotes, unusual fonts, and spatial layout.

## Setup

The tool requires Python 3.10 or newer and PyMuPDF:

```powershell
python -m pip install -r tools/reference-requirements.txt
```

Codex's bundled PDF runtime already provides this dependency.

## Common operations

List and verify cataloged documents:

```powershell
python tools/reference_cache.py list
python tools/reference_cache.py status
```

Build all page-text caches, or only one source:

```powershell
python tools/reference_cache.py build
python tools/reference_cache.py build n32l40x-um-v2.6
```

Build text and the source's deliberately selected page renders:

```powershell
python tools/reference_cache.py build n32l40x-um-v2.6 --render-selected
```

Search across cached page text. All whitespace-separated terms must appear on
the same PDF page, but they do not need to be adjacent:

```powershell
python tools/reference_cache.py search "SRAM2 parity"
python tools/reference_cache.py search OCREF --source n32l40x-um-v2.6
```

Read or render a specific one-based PDF page:

```powershell
python tools/reference_cache.py show n32l40x-um-v2.6 46
python tools/reference_cache.py show n32l40x-um-v2.6 46 --render
python tools/reference_cache.py render servo57d-rs485-sch-v1.1 1 --dpi 240
```

Use `--force` only when deliberately rebuilding a current text cache or page
render. Normal builds reuse cache entries whose source digest, cache schema,
page count, and expected files are current.

## Adding a document

Add a row to `reference/sources.json` with a stable lowercase ID, title,
version, kind, project-relative local path, SHA-256 digest, page count, and any
pages worth pre-rendering. Then run `status` before `build`. A changed PDF must
be treated as a new reviewed version: update its catalog metadata deliberately
rather than bypassing the digest check.

Do not commit the original PDFs, extracted page text, renders, or crops unless
redistribution rights have been established. Project-owned conclusions belong
in `docs/` and should cite the stable source ID and one-based PDF page.
