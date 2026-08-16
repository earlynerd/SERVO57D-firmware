# Reference Material

`reference/local/` contains locally cached external schematics, manuals, datasheets, CMSIS packs, and derived research images. The directory is ignored by Git so the future public repository does not accidentally redistribute external documents.

The versions and hashes needed to reconstruct the research environment are recorded in [the external reference inventory](../docs/REFERENCE_INVENTORY.md). Add canonical source URLs there before publishing the repository.

`sources.json` is the machine-readable catalog for repeatedly used PDFs.
`tools/reference_cache.py` verifies those sources and creates an ignored
`reference/cache/` containing searchable page text and on-demand renders. See
[the local reference-cache workflow](../docs/REFERENCE_CACHE.md).
