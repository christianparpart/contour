"""The pages generated at build time are written by generate-reference.sh and ignored by git, all in agreement."""

import unittest
from pathlib import Path

from mkdocs.config import load_config

ROOT = Path(__file__).resolve().parents[2]
# Included as a snippet by other pages, so it is not a page of its own and not listed under generated_pages.
SNIPPET_PAGES = ["configuration/key-mapping-actions.md"]


def generated_pages() -> list[str]:
    return list(load_config(str(ROOT / "mkdocs.yml"))["extra"]["generated_pages"])


class GeneratedPagesTest(unittest.TestCase):
    def test_every_generated_page_is_written_and_ignored(self) -> None:
        listed = generated_pages()
        self.assertTrue(listed)
        script = (ROOT / "website/tools/generate-reference.sh").read_text(encoding="utf-8")
        ignored = (ROOT / ".gitignore").read_text(encoding="utf-8").splitlines()
        for page in [*listed, *SNIPPET_PAGES]:
            with self.subTest(page=page):
                self.assertIn(f"docs/{page}", script, "generate-reference.sh does not write it")
                self.assertTrue(f"docs/{page}" in ignored, ".gitignore does not list it")

    def test_every_generated_file_is_a_listed_page(self) -> None:
        """Conversely, a file the script writes under docs/ is either listed or a known snippet."""
        known = {*generated_pages(), *SNIPPET_PAGES}
        script = (ROOT / "website/tools/generate-reference.sh").read_text(encoding="utf-8")
        written = {
            word.removeprefix("docs/")
            for line in script.splitlines()
            for word in line.replace(">", " ").split()
            if word.startswith("docs/") and word.endswith(".md")
        }
        self.assertEqual(written, known)


if __name__ == "__main__":
    unittest.main()
