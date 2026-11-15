"""Unit tests for check_site.py; each test builds a fake site in a temporary directory."""

import contextlib
import io
import tempfile
import unittest
from pathlib import Path

import check_site


def write(root: Path, rel: str, content: str = "") -> Path:
    path = root / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    return path


class CheckSiteTest(unittest.TestCase):
    BLOB = "https://github.com/contour-terminal/contour/blob/master/docs/"

    def redirects(self, maps: dict[str, str]) -> list[str]:
        return check_site.check_redirects(self.site, self.root / "docs", self.BLOB, maps, True)

    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name)
        self.site = self.root / "site"
        self.site.mkdir()

    def test_html_path(self) -> None:
        self.assertEqual(check_site.html_path("index.md", True), "index.html")
        self.assertEqual(check_site.html_path("install.md", True), "install/index.html")
        self.assertEqual(check_site.html_path("install.md", False), "install.html")
        self.assertEqual(
            check_site.html_path("vt-sequence/index.md", True), "vt-sequence/index.html"
        )

    def test_excluded_prefixes_come_from_exclude_docs(self) -> None:
        patterns = ["internals/", "CONTRIBUTING.md", "/drafts/", "a/b.md", "*.tmp", "!keep/", "# note", ""]
        self.assertEqual(
            check_site.excluded_prefixes(patterns), ["internals/", "CONTRIBUTING", "drafts/", "a/b"]
        )

    def test_pages_are_sorted_and_skip_excluded(self) -> None:
        for rel in ("b/index.html", "a/index.html", "internals/x/index.html", "a/data.json"):
            write(self.site, rel)
        pages = [rel for _, rel in check_site._pages(self.site, ["internals/"])]
        self.assertEqual(pages, ["a/index.html", "b/index.html"])

    def test_exclusion_respects_path_boundaries(self) -> None:
        excluded = ["internals/", "CONTRIBUTING", "configuration/key-mapping-actions"]
        for rel in ("internals/x/index.html", "CONTRIBUTING/index.html", "CONTRIBUTING.html",
                    "configuration/key-mapping-actions/index.html"):
            self.assertTrue(check_site.is_excluded(rel, excluded), rel)
        for rel in ("CONTRIBUTING-guide/index.html", "internals-faq/index.html",
                    "configuration/key-mapping-actions-extra/index.html"):
            self.assertFalse(check_site.is_excluded(rel, excluded), rel)
        write(self.site, "configuration/key-mapping-actions-extra/index.html")
        pages = [rel for _, rel in check_site._pages(self.site, excluded)]
        self.assertEqual(pages, ["configuration/key-mapping-actions-extra/index.html"])

    def test_nav_page_missing(self) -> None:
        errors = check_site.check_nav_pages(self.site, ["install.md"], True)
        self.assertEqual(len(errors), 1)
        self.assertIn("install.md", errors[0])

    def test_redirect_anchor_missing(self) -> None:
        maps = {"demo/ime.md": "features.md#ime"}
        write(self.site, "features/index.html", "<h2>IME</h2>")
        self.assertEqual(len(self.redirects(maps)), 1)
        write(self.site, "features/index.html", '<h2 id="ime">IME</h2>')
        self.assertEqual(self.redirects(maps), [])

    def test_redirect_external_must_exist_in_repo(self) -> None:
        url = self.BLOB + "internals/x.md"
        maps = {"internals/x.md": url}
        self.assertEqual(len(self.redirects(maps)), 1)
        write(self.root, "docs/internals/x.md", "x")
        self.assertEqual(self.redirects(maps), [])
        other = {"internals/x.md": "https://example.com/x"}
        self.assertEqual(len(self.redirects(other)), 1)
        # Same repository, but outside docs/: not a redirect target the site may rely on.
        outside = {"internals/x.md": "https://github.com/contour-terminal/contour/blob/master/README.md"}
        self.assertEqual(len(self.redirects(outside)), 1)

    def test_link_into_excluded_path(self) -> None:
        write(self.site, "install/index.html", '<a href="../internals/vthost/">x</a>')
        write(self.site, "internals/vthost/index.html", "")
        errors = check_site.check_links(self.site, ["internals/"])
        self.assertEqual(len(errors), 1)
        self.assertIn("install/index.html", errors[0])

    def test_link_into_excluded_directory_without_file_name(self) -> None:
        write(self.site, "install/index.html", '<a href="../internals/">x</a>')
        write(self.site, "internals/index.html", "")
        errors = check_site.check_links(self.site, ["internals/"])
        self.assertEqual(len(errors), 1)
        self.assertIn("excluded", errors[0])

    def test_percent_encoded_link(self) -> None:
        write(self.site, "a/index.html", '<img src="a%20b.png">')
        write(self.site, "a/a b.png", "x")
        self.assertEqual(check_site.check_links(self.site, []), [])

    def test_broken_relative_link(self) -> None:
        write(self.site, "a/index.html", '<a href="../nope/">x</a>')
        self.assertEqual(len(check_site.check_links(self.site, [])), 1)
        write(self.site, "install/index.html", "")
        write(
            self.site,
            "a/index.html",
            '<a href="../install/#x">1</a><a href="https://e.com/">2</a>'
            '<a href="mailto:a@b.c">3</a><a href="#top">4</a>',
        )
        self.assertEqual(check_site.check_links(self.site, []), [])

    def test_orphan_page(self) -> None:
        write(self.site, "install/index.html", "")
        write(self.site, "csi-u/index.html", "")
        write(self.site, "old/index.html", '<meta http-equiv="refresh" content="0">')
        write(self.site, "404.html", "")
        errors = check_site.check_orphans(self.site, ["install.md"], True)
        self.assertEqual(len(errors), 1)
        self.assertIn("csi-u", errors[0])

    def test_weight(self) -> None:
        write(self.site, "assets/theme/a.css", "a" * 60000)
        write(self.site, "assets/theme/js/b.js", "b" * 1441)
        write(self.site, "search/lunr.js", "c" * 100000)
        self.assertEqual(len(check_site.check_weight(self.site)), 1)
        write(self.site, "assets/theme/js/b.js", "b" * 1440)
        self.assertEqual(check_site.check_weight(self.site), [])

    def test_cname(self) -> None:
        self.assertEqual(len(check_site.check_cname(self.site)), 1)
        write(self.site, "CNAME", "www.contour-terminal.org\n")
        self.assertEqual(len(check_site.check_cname(self.site)), 1)
        write(self.site, "CNAME", "contour-terminal.org\n")
        self.assertEqual(check_site.check_cname(self.site), [])

    def test_duplicate_ids(self) -> None:
        write(self.site, "a/index.html", '<main id="cw-page"></main><h3 id="theme">t</h3>')
        self.assertEqual(check_site.check_unique_ids(self.site, []), [])
        write(self.site, "a/index.html", '<button id="theme"></button><h3 id="theme">t</h3>')
        errors = check_site.check_unique_ids(self.site, [])
        self.assertEqual(len(errors), 1)
        self.assertIn("a/index.html", errors[0])
        self.assertEqual(check_site.check_unique_ids(self.site, ["a/"]), [])

    def test_current_tab(self) -> None:
        def page(tabs: str, ws: str = '<a class="ws" aria-current="page">w</a>') -> str:
            return f'<nav>{ws}</nav><div id="cw-tabs">{tabs}</div>'

        tab = '<a class="tab" aria-current="page">t</a>'
        write(self.site, "install/index.html", page(""))
        self.assertEqual(len(check_site.check_current(self.site, ["install.md"], True)), 1)
        write(self.site, "install/index.html", page(tab + tab))
        self.assertEqual(len(check_site.check_current(self.site, ["install.md"], True)), 1)
        write(self.site, "install/index.html", page(tab))
        self.assertEqual(check_site.check_current(self.site, ["install.md"], True), [])
        ws = '<a class="ws" aria-current="page">w</a>'
        write(self.site, "install/index.html", page(tab, ws=""))
        self.assertEqual(len(check_site.check_current(self.site, ["install.md"], True)), 1)
        write(self.site, "install/index.html", page(tab, ws=ws + ws))
        self.assertEqual(len(check_site.check_current(self.site, ["install.md"], True)), 1)
        # A current tab outside #cw-tabs does not count.
        write(self.site, "install/index.html", page("") + tab)
        self.assertEqual(len(check_site.check_current(self.site, ["install.md"], True)), 1)
        write(self.site, "install/index.html", page(tab) + tab)
        self.assertEqual(check_site.check_current(self.site, ["install.md"], True), [])


    def test_main_checks_a_whole_site(self) -> None:
        """The theme checks run on every site, whether or not it carries assets/theme."""
        write(self.root, "docs/index.md", "# Home\n")
        write(self.root, "docs/CNAME", "contour-terminal.org\n")
        (self.root / "theme").mkdir()
        write(
            self.root,
            "mkdocs.yml",
            "site_name: t\nrepo_url: https://github.com/contour-terminal/contour/\n"
            "nav: [index.md]\ntheme: {name: null, custom_dir: theme}\nexclude_docs: |\n  drafts/\n",
        )
        tabs = '<nav id="cw-tabs"><a class="tab" aria-current="page">t</a></nav>'
        write(self.site, "index.html", tabs)
        write(self.site, "CNAME", "contour-terminal.org\n")
        argv = ["--config", str(self.root / "mkdocs.yml"), "--site", str(self.site)]
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(check_site.main(argv), 1)
        self.assertIn("current workspace", out.getvalue())
        write(self.site, "index.html", '<a class="ws" aria-current="true">w</a>' + tabs)
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(check_site.main(argv), 0, out.getvalue())


if __name__ == "__main__":
    unittest.main()
