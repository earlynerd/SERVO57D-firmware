from __future__ import annotations

import contextlib
import hashlib
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS_DIR))

import reference_cache


PYMUPDF_AVAILABLE = importlib.util.find_spec("pymupdf") is not None


@unittest.skipUnless(PYMUPDF_AVAILABLE, "PyMuPDF is not installed")
class ReferenceCacheIntegrationTests(unittest.TestCase):
    def setUp(self) -> None:
        import pymupdf

        self.temporary = tempfile.TemporaryDirectory(prefix="mks57d-reference-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        local_dir = self.root / "reference" / "local"
        local_dir.mkdir(parents=True)
        self.pdf_path = local_dir / "sample.pdf"

        document = pymupdf.open()
        page = document.new_page()
        page.insert_text((72, 72), "ADC timer trigger and SRAM2 parity")
        document.save(self.pdf_path)
        document.close()

        digest = hashlib.sha256(self.pdf_path.read_bytes()).hexdigest()
        catalog_data = {
            "schema_version": 1,
            "cache_directory": "reference/cache",
            "sources": [
                {
                    "id": "sample-v1",
                    "title": "Sample Reference",
                    "version": "1.0",
                    "kind": "test-document",
                    "path": "reference/local/sample.pdf",
                    "sha256": digest,
                    "pages": 1,
                    "render_pages": [1],
                }
            ],
        }
        self.catalog_path = self.root / "reference" / "sources.json"
        self.catalog_path.write_text(
            json.dumps(catalog_data, indent=2), encoding="utf-8"
        )
        self.catalog = reference_cache.load_catalog(self.catalog_path)
        self.source = self.catalog.source("sample-v1")

    def test_build_search_and_status(self) -> None:
        with contextlib.redirect_stdout(io.StringIO()):
            reference_cache.build_source(
                self.catalog,
                self.source,
                force=False,
                render_selected=False,
                dpi=144,
            )

        page_path = (
            self.catalog.cache_root / "sample-v1" / "pages" / "0001.txt"
        )
        self.assertTrue(page_path.is_file())
        self.assertIn("SRAM2 parity", reference_cache.cached_page_body(page_path))

        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            result = reference_cache.search_cache(
                self.catalog,
                "ADC parity",
                [],
                limit=5,
                context=80,
                case_sensitive=False,
            )
        self.assertEqual(result, 0)
        self.assertIn("sample-v1: PDF page 1", output.getvalue())

        with contextlib.redirect_stdout(io.StringIO()):
            status = reference_cache.print_status(self.catalog, [self.source])
        self.assertEqual(status, 0)

    def test_render_writes_png_and_provenance_sidecar(self) -> None:
        with contextlib.redirect_stdout(io.StringIO()):
            output = reference_cache.render_page(
                self.catalog,
                self.source,
                1,
                dpi=144,
                force=False,
            )
        self.assertTrue(output.is_file())
        self.assertEqual(output.read_bytes()[:8], b"\x89PNG\r\n\x1a\n")
        sidecar = json.loads(output.with_suffix(".json").read_text(encoding="utf-8"))
        self.assertEqual(sidecar["source_id"], "sample-v1")
        self.assertEqual(sidecar["pdf_page"], 1)
        self.assertEqual(sidecar["dpi"], 144)


if __name__ == "__main__":
    unittest.main()
