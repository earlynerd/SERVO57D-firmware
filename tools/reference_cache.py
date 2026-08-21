#!/usr/bin/env python3
"""Build and query a local, page-addressable cache of PDF references."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


CACHE_SCHEMA = 1
CATALOG_SCHEMA = 1
SOURCE_ID_RE = re.compile(r"^[a-z0-9][a-z0-9.-]*$")
SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")


class ReferenceCacheError(RuntimeError):
    """Expected configuration, dependency, or source-document failure."""


@dataclass(frozen=True)
class Source:
    source_id: str
    title: str
    version: str
    relative_path: str
    path: Path
    expected_sha256: str
    expected_pages: int
    render_pages: tuple[int, ...]
    kind: str


@dataclass(frozen=True)
class Catalog:
    path: Path
    project_root: Path
    cache_root: Path
    sources: tuple[Source, ...]

    def source(self, source_id: str) -> Source:
        for source in self.sources:
            if source.source_id == source_id:
                return source
        known = ", ".join(source.source_id for source in self.sources)
        raise ReferenceCacheError(
            f"Unknown source ID {source_id!r}. Known source IDs: {known}"
        )


def configure_console() -> None:
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure is not None:
            reconfigure(encoding="utf-8", errors="replace")


def resolve_within(root: Path, relative_value: str, label: str) -> Path:
    relative = Path(relative_value)
    if relative.is_absolute():
        raise ReferenceCacheError(f"{label} must be relative to the project root")
    root = root.resolve()
    resolved = (root / relative).resolve()
    try:
        resolved.relative_to(root)
    except ValueError as exc:
        raise ReferenceCacheError(f"{label} escapes the project root") from exc
    return resolved


def load_catalog(catalog_path: Path) -> Catalog:
    catalog_path = catalog_path.resolve()
    if not catalog_path.is_file():
        raise ReferenceCacheError(f"Catalog not found: {catalog_path}")

    try:
        raw = json.loads(catalog_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ReferenceCacheError(f"Cannot read catalog {catalog_path}: {exc}") from exc

    if not isinstance(raw, dict) or raw.get("schema_version") != CATALOG_SCHEMA:
        raise ReferenceCacheError(
            f"Catalog must use schema_version {CATALOG_SCHEMA}"
        )

    # The tracked catalog lives at <project>/reference/sources.json. Keeping
    # this relationship also permits isolated tests with a temporary project.
    project_root = catalog_path.parent.parent.resolve()
    cache_value = raw.get("cache_directory", "reference/cache")
    if not isinstance(cache_value, str) or not cache_value:
        raise ReferenceCacheError("cache_directory must be a non-empty string")
    cache_root = resolve_within(project_root, cache_value, "cache_directory")
    if cache_root == project_root:
        raise ReferenceCacheError("cache_directory cannot be the project root")

    source_rows = raw.get("sources")
    if not isinstance(source_rows, list) or not source_rows:
        raise ReferenceCacheError("Catalog must contain at least one source")

    sources: list[Source] = []
    seen_ids: set[str] = set()
    for index, row in enumerate(source_rows):
        label = f"sources[{index}]"
        if not isinstance(row, dict):
            raise ReferenceCacheError(f"{label} must be an object")

        source_id = row.get("id")
        if not isinstance(source_id, str) or not SOURCE_ID_RE.fullmatch(source_id):
            raise ReferenceCacheError(f"{label}.id is not a valid stable source ID")
        if source_id in seen_ids:
            raise ReferenceCacheError(f"Duplicate source ID: {source_id}")
        seen_ids.add(source_id)

        title = row.get("title")
        version = row.get("version")
        relative_path = row.get("path")
        expected_sha256 = row.get("sha256")
        expected_pages = row.get("pages")
        kind = row.get("kind", "document")
        render_pages = row.get("render_pages", [])

        for field_name, value in (
            ("title", title),
            ("version", version),
            ("path", relative_path),
            ("kind", kind),
        ):
            if not isinstance(value, str) or not value:
                raise ReferenceCacheError(f"{label}.{field_name} must be non-empty")
        if not isinstance(expected_sha256, str) or not SHA256_RE.fullmatch(
            expected_sha256
        ):
            raise ReferenceCacheError(f"{label}.sha256 must be a SHA-256 digest")
        if not isinstance(expected_pages, int) or expected_pages < 1:
            raise ReferenceCacheError(f"{label}.pages must be a positive integer")
        if not isinstance(render_pages, list) or any(
            not isinstance(page, int) or page < 1 or page > expected_pages
            for page in render_pages
        ):
            raise ReferenceCacheError(
                f"{label}.render_pages must contain valid one-based PDF pages"
            )

        sources.append(
            Source(
                source_id=source_id,
                title=title,
                version=version,
                relative_path=relative_path,
                path=resolve_within(project_root, relative_path, f"{label}.path"),
                expected_sha256=expected_sha256.lower(),
                expected_pages=expected_pages,
                render_pages=tuple(sorted(set(render_pages))),
                kind=kind,
            )
        )

    return Catalog(
        path=catalog_path,
        project_root=project_root,
        cache_root=cache_root,
        sources=tuple(sources),
    )


def require_pymupdf() -> Any:
    try:
        import pymupdf  # type: ignore[import-not-found]
    except ImportError as exc:
        raise ReferenceCacheError(
            "PyMuPDF is required for build and render operations. Install the "
            "dependencies from tools/reference-requirements.txt."
        ) from exc
    return pymupdf


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify_source_file(source: Source) -> str:
    if not source.path.is_file():
        raise ReferenceCacheError(
            f"{source.source_id}: local PDF is missing: {source.relative_path}"
        )
    actual_sha256 = sha256_file(source.path)
    if actual_sha256 != source.expected_sha256:
        raise ReferenceCacheError(
            f"{source.source_id}: SHA-256 mismatch; expected "
            f"{source.expected_sha256}, found {actual_sha256}"
        )
    return actual_sha256


def source_cache_dir(catalog: Catalog, source: Source) -> Path:
    cache_dir = (catalog.cache_root / source.source_id).resolve()
    try:
        cache_dir.relative_to(catalog.cache_root.resolve())
    except ValueError as exc:
        raise ReferenceCacheError("Resolved source cache escapes cache root") from exc
    return cache_dir


def read_manifest(catalog: Catalog, source: Source) -> dict[str, Any] | None:
    path = source_cache_dir(catalog, source) / "manifest.json"
    if not path.is_file():
        return None
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    return manifest if isinstance(manifest, dict) else None


def manifest_is_current(source: Source, manifest: dict[str, Any] | None) -> bool:
    if manifest is None:
        return False
    return (
        manifest.get("cache_schema") == CACHE_SCHEMA
        and manifest.get("source_id") == source.source_id
        and manifest.get("source_sha256") == source.expected_sha256
        and manifest.get("page_count") == source.expected_pages
        and isinstance(manifest.get("pages"), list)
        and len(manifest["pages"]) == source.expected_pages
    )


def cache_files_are_present(cache_dir: Path, manifest: dict[str, Any]) -> bool:
    for page in manifest.get("pages", []):
        if not isinstance(page, dict) or not isinstance(page.get("file"), str):
            return False
        page_path = (cache_dir / page["file"]).resolve()
        try:
            page_path.relative_to(cache_dir.resolve())
        except ValueError:
            return False
        if not page_path.is_file():
            return False
    return True


def atomic_write_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp")
    temporary.write_text(content, encoding="utf-8", newline="\n")
    temporary.replace(path)


def page_cache_content(
    source: Source, page_number: int, extractor: str, text: str
) -> str:
    header = {
        "cache_schema": CACHE_SCHEMA,
        "source_id": source.source_id,
        "source_title": source.title,
        "source_version": source.version,
        "pdf_page": page_number,
        "source_sha256": source.expected_sha256,
        "extractor": extractor,
        "notice": "Generated local cache; confirm layout-sensitive claims against the PDF.",
    }
    normalized = text.replace("\r\n", "\n").replace("\r", "\n").replace("\x00", "")
    return json.dumps(header, ensure_ascii=False, indent=2) + "\n---\n" + normalized.strip() + "\n"


def build_source(
    catalog: Catalog,
    source: Source,
    *,
    force: bool,
    render_selected: bool,
    dpi: int,
) -> None:
    source_sha256 = verify_source_file(source)
    cache_dir = source_cache_dir(catalog, source)
    manifest = read_manifest(catalog, source)
    current = (
        manifest_is_current(source, manifest)
        and manifest is not None
        and cache_files_are_present(cache_dir, manifest)
    )

    if current and not force:
        print(f"{source.source_id}: text cache is current")
    else:
        pymupdf = require_pymupdf()
        document = pymupdf.open(source.path)
        try:
            if document.page_count != source.expected_pages:
                raise ReferenceCacheError(
                    f"{source.source_id}: expected {source.expected_pages} pages, "
                    f"found {document.page_count}"
                )

            page_width = max(4, len(str(document.page_count)))
            pages_dir = cache_dir / "pages"
            page_rows: list[dict[str, Any]] = []
            extractor = f"PyMuPDF {pymupdf.__version__}"
            for page_index in range(document.page_count):
                page_number = page_index + 1
                page_text = document.load_page(page_index).get_text("text", sort=True)
                relative_file = f"pages/{page_number:0{page_width}d}.txt"
                atomic_write_text(
                    cache_dir / relative_file,
                    page_cache_content(source, page_number, extractor, page_text),
                )
                page_rows.append(
                    {
                        "pdf_page": page_number,
                        "file": relative_file,
                        "characters": len(page_text),
                        "has_text": bool(page_text.strip()),
                    }
                )
                if page_number % 100 == 0 or page_number == document.page_count:
                    print(
                        f"{source.source_id}: extracted {page_number}/"
                        f"{document.page_count} pages"
                    )

            manifest = {
                "cache_schema": CACHE_SCHEMA,
                "source_id": source.source_id,
                "title": source.title,
                "version": source.version,
                "kind": source.kind,
                "source_path": source.relative_path,
                "source_sha256": source_sha256,
                "page_count": document.page_count,
                "extractor": extractor,
                "built_at_utc": datetime.now(timezone.utc).isoformat(),
                "pages": page_rows,
            }
            atomic_write_text(
                cache_dir / "manifest.json",
                json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
            )
            print(f"{source.source_id}: wrote searchable text cache to {cache_dir}")
        finally:
            document.close()

    if render_selected:
        for page_number in source.render_pages:
            render_page(
                catalog,
                source,
                page_number,
                dpi=dpi,
                force=force,
                verified_sha256=source_sha256,
            )


def render_page(
    catalog: Catalog,
    source: Source,
    page_number: int,
    *,
    dpi: int,
    force: bool,
    verified_sha256: str | None = None,
) -> Path:
    if page_number < 1 or page_number > source.expected_pages:
        raise ReferenceCacheError(
            f"{source.source_id}: page must be between 1 and {source.expected_pages}"
        )
    if dpi < 72 or dpi > 600:
        raise ReferenceCacheError("Render DPI must be between 72 and 600")

    source_sha256 = verified_sha256 or verify_source_file(source)
    pymupdf = require_pymupdf()
    cache_dir = source_cache_dir(catalog, source)
    page_width = max(4, len(str(source.expected_pages)))
    output = cache_dir / "renders" / f"{page_number:0{page_width}d}-{dpi}dpi.png"
    sidecar = output.with_suffix(".json")
    expected_sidecar = {
        "cache_schema": CACHE_SCHEMA,
        "source_id": source.source_id,
        "source_sha256": source_sha256,
        "pdf_page": page_number,
        "dpi": dpi,
        "renderer": f"PyMuPDF {pymupdf.__version__}",
    }
    if output.is_file() and sidecar.is_file() and not force:
        sidecar_is_current = False
        try:
            sidecar_is_current = (
                json.loads(sidecar.read_text(encoding="utf-8"))
                == expected_sidecar
            )
        except (OSError, json.JSONDecodeError):
            sidecar_is_current = False
        if sidecar_is_current:
            print(f"{source.source_id}: page {page_number} render is current")
            return output

    output.parent.mkdir(parents=True, exist_ok=True)
    document = pymupdf.open(source.path)
    try:
        if document.page_count != source.expected_pages:
            raise ReferenceCacheError(
                f"{source.source_id}: expected {source.expected_pages} pages, "
                f"found {document.page_count}"
            )
        page = document.load_page(page_number - 1)
        scale = dpi / 72.0
        estimated_pixels = int(page.rect.width * scale) * int(page.rect.height * scale)
        if estimated_pixels > 80_000_000:
            raise ReferenceCacheError(
                "Requested render would exceed the 80-megapixel safety limit"
            )
        pixmap = page.get_pixmap(matrix=pymupdf.Matrix(scale, scale), alpha=False)
        temporary = output.with_name(f".{output.stem}.tmp.png")
        pixmap.save(temporary)
        temporary.replace(output)
        atomic_write_text(
            sidecar, json.dumps(expected_sidecar, ensure_ascii=False, indent=2) + "\n"
        )
    finally:
        document.close()
    print(f"{source.source_id}: rendered PDF page {page_number} to {output}")
    return output


def selected_sources(catalog: Catalog, source_ids: Iterable[str]) -> list[Source]:
    ids = list(source_ids)
    if not ids:
        return list(catalog.sources)
    return [catalog.source(source_id) for source_id in ids]


def cached_page_body(path: Path) -> str:
    content = path.read_text(encoding="utf-8")
    separator = "\n---\n"
    if separator not in content:
        raise ReferenceCacheError(f"Malformed cached page: {path}")
    return content.split(separator, 1)[1]


def search_cache(
    catalog: Catalog,
    query: str,
    source_ids: Iterable[str],
    *,
    limit: int,
    context: int,
    case_sensitive: bool,
) -> int:
    terms = [term for term in query.split() if term]
    if not terms:
        raise ReferenceCacheError("Search query cannot be empty")
    if limit < 1:
        raise ReferenceCacheError("Search limit must be positive")
    if context < 40 or context > 2000:
        raise ReferenceCacheError("Search context must be between 40 and 2000")

    search_terms = terms if case_sensitive else [term.casefold() for term in terms]
    results: list[tuple[int, str, int, str]] = []
    for source in selected_sources(catalog, source_ids):
        try:
            verify_source_file(source)
        except ReferenceCacheError as exc:
            print(f"warning: {exc}", file=sys.stderr)
            continue
        manifest = read_manifest(catalog, source)
        cache_dir = source_cache_dir(catalog, source)
        if not manifest_is_current(source, manifest) or manifest is None:
            print(
                f"warning: {source.source_id}: cache is missing or stale; run build",
                file=sys.stderr,
            )
            continue
        for page_row in manifest["pages"]:
            page_path = cache_dir / page_row["file"]
            if not page_path.is_file():
                continue
            body = cached_page_body(page_path)
            compact = " ".join(body.split())
            haystack = compact if case_sensitive else compact.casefold()
            if not all(term in haystack for term in search_terms):
                continue
            positions = [haystack.find(term) for term in search_terms]
            first = min(position for position in positions if position >= 0)
            start = max(0, first - context // 2)
            end = min(len(compact), start + context)
            snippet = compact[start:end]
            if start:
                snippet = "…" + snippet
            if end < len(compact):
                snippet += "…"
            score = sum(haystack.count(term) for term in search_terms)
            results.append(
                (score, source.source_id, int(page_row["pdf_page"]), snippet)
            )

    results.sort(key=lambda item: (-item[0], item[1], item[2]))
    for score, source_id, page_number, snippet in results[:limit]:
        print(f"{source_id}: PDF page {page_number} (score {score})")
        print(f"  {snippet}")
    if not results:
        print("No matching cached pages found.")
        return 1
    if len(results) > limit:
        print(f"Showing {limit} of {len(results)} matching pages.")
    return 0


def show_page(
    catalog: Catalog,
    source: Source,
    page_number: int,
    *,
    render: bool,
    dpi: int,
) -> None:
    verify_source_file(source)
    manifest = read_manifest(catalog, source)
    if not manifest_is_current(source, manifest) or manifest is None:
        raise ReferenceCacheError(
            f"{source.source_id}: cache is missing or stale; run build first"
        )
    if page_number < 1 or page_number > source.expected_pages:
        raise ReferenceCacheError(
            f"{source.source_id}: page must be between 1 and {source.expected_pages}"
        )
    page_row = manifest["pages"][page_number - 1]
    page_path = source_cache_dir(catalog, source) / page_row["file"]
    print(f"{source.title} {source.version} — PDF page {page_number}")
    print(f"Source: {source.relative_path}")
    print()
    print(cached_page_body(page_path).rstrip())
    if render:
        print()
        output = render_page(catalog, source, page_number, dpi=dpi, force=False)
        print(f"Render: {output}")


def print_status(catalog: Catalog, sources: Iterable[Source]) -> int:
    failed = False
    for source in sources:
        if not source.path.is_file():
            print(f"{source.source_id}: source missing — {source.relative_path}")
            failed = True
            continue
        actual_sha256 = sha256_file(source.path)
        if actual_sha256 != source.expected_sha256:
            print(f"{source.source_id}: source hash mismatch")
            failed = True
            continue
        manifest = read_manifest(catalog, source)
        cache_dir = source_cache_dir(catalog, source)
        if (
            manifest_is_current(source, manifest)
            and manifest is not None
            and cache_files_are_present(cache_dir, manifest)
        ):
            cache_state = "cache current"
        else:
            cache_state = "cache not built"
        print(
            f"{source.source_id}: source verified, {source.expected_pages} pages, "
            f"{cache_state}"
        )
    return 1 if failed else 0


def build_parser(default_catalog: Path) -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Build and query an ignored local cache of PDF references."
    )
    parser.add_argument(
        "--catalog",
        type=Path,
        default=default_catalog,
        help="source catalog (default: reference/sources.json)",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    list_parser = subparsers.add_parser("list", help="list cataloged sources")
    list_parser.set_defaults(command_name="list")

    status_parser = subparsers.add_parser("status", help="verify sources and cache state")
    status_parser.add_argument("sources", nargs="*", metavar="SOURCE_ID")

    build_parser = subparsers.add_parser("build", help="extract searchable page text")
    build_parser.add_argument("sources", nargs="*", metavar="SOURCE_ID")
    build_parser.add_argument("--force", action="store_true")
    build_parser.add_argument("--render-selected", action="store_true")
    build_parser.add_argument("--dpi", type=int, default=180)

    search_parser = subparsers.add_parser("search", help="search cached page text")
    search_parser.add_argument("query", nargs="+")
    search_parser.add_argument("--source", action="append", default=[])
    search_parser.add_argument("--limit", type=int, default=20)
    search_parser.add_argument("--context", type=int, default=240)
    search_parser.add_argument("--case-sensitive", action="store_true")

    show_parser = subparsers.add_parser("show", help="show one cached PDF page")
    show_parser.add_argument("source")
    show_parser.add_argument("page", type=int)
    show_parser.add_argument("--render", action="store_true")
    show_parser.add_argument("--dpi", type=int, default=180)

    render_parser = subparsers.add_parser("render", help="render one PDF page")
    render_parser.add_argument("source")
    render_parser.add_argument("page", type=int)
    render_parser.add_argument("--dpi", type=int, default=180)
    render_parser.add_argument("--force", action="store_true")

    selected_parser = subparsers.add_parser(
        "render-selected", help="render the catalog's selected pages"
    )
    selected_parser.add_argument("sources", nargs="*", metavar="SOURCE_ID")
    selected_parser.add_argument("--dpi", type=int, default=180)
    selected_parser.add_argument("--force", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    configure_console()
    default_catalog = Path(__file__).resolve().parents[1] / "reference" / "sources.json"
    parser = build_parser(default_catalog)
    args = parser.parse_args(argv)
    try:
        catalog = load_catalog(args.catalog)
        if args.command == "list":
            for source in catalog.sources:
                print(
                    f"{source.source_id}\n  {source.title} {source.version}\n"
                    f"  {source.relative_path}"
                )
            return 0
        if args.command == "status":
            return print_status(catalog, selected_sources(catalog, args.sources))
        if args.command == "build":
            for source in selected_sources(catalog, args.sources):
                build_source(
                    catalog,
                    source,
                    force=args.force,
                    render_selected=args.render_selected,
                    dpi=args.dpi,
                )
            return 0
        if args.command == "search":
            return search_cache(
                catalog,
                " ".join(args.query),
                args.source,
                limit=args.limit,
                context=args.context,
                case_sensitive=args.case_sensitive,
            )
        if args.command == "show":
            show_page(
                catalog,
                catalog.source(args.source),
                args.page,
                render=args.render,
                dpi=args.dpi,
            )
            return 0
        if args.command == "render":
            render_page(
                catalog,
                catalog.source(args.source),
                args.page,
                dpi=args.dpi,
                force=args.force,
            )
            return 0
        if args.command == "render-selected":
            for source in selected_sources(catalog, args.sources):
                source_sha256 = verify_source_file(source)
                for page_number in source.render_pages:
                    render_page(
                        catalog,
                        source,
                        page_number,
                        dpi=args.dpi,
                        force=args.force,
                        verified_sha256=source_sha256,
                    )
            return 0
        parser.error(f"Unhandled command: {args.command}")
    except ReferenceCacheError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
