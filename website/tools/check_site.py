#!/usr/bin/env python3
"""Checks a built MkDocs site: nav pages, redirects, links, orphans, theme weight, current markers.

Every check returns a list of error messages; an empty list means the check passed.
"""

import argparse
import posixpath
import re
import sys
from collections.abc import Iterator
from urllib.parse import unquote
from html.parser import HTMLParser
from pathlib import Path

WEIGHT_LIMIT = 61440
# The domain the site is served from; docs/CNAME must keep it, or the first deploy moves the site.
CUSTOM_DOMAIN = "contour-terminal.org"
# Directories that hold build artefacts rather than pages.
NON_PAGE_DIRS = ("assets/", "search/", "css/", "js/", "img/")


def excluded_prefixes(patterns: list[str]) -> list[str]:
    """Site-path prefixes of the pages mkdocs `exclude_docs` keeps out of the build.

    Handles the plain directory and file entries the config uses; glob and negation patterns are skipped.
    """
    return [
        pattern.lstrip("/").removesuffix(".md")
        for pattern in patterns
        if pattern and not pattern.startswith(("!", "#")) and not any(c in pattern for c in "*?[")
    ]


def is_excluded(path: str, excluded: list[str]) -> bool:
    """True when a site path is an excluded page or lies under an excluded directory (prefix ending in `/`)."""
    for prefix in excluded:
        if prefix.endswith("/"):
            if (path + "/").startswith(prefix):
                return True
        elif path in (prefix, f"{prefix}.html") or path.startswith(f"{prefix}/"):
            return True
    return False


def html_path(md: str, directory_urls: bool) -> str:
    """Maps a docs-relative Markdown path to the HTML file MkDocs builds for it."""
    stem = md.removesuffix(".md")
    if stem == "index" or stem.endswith("/index"):
        return stem + ".html"
    return f"{stem}/index.html" if directory_urls else f"{stem}.html"


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def check_nav_pages(site_dir: Path, nav_files: list[str], directory_urls: bool) -> list[str]:
    """Every nav page must have been built."""
    return [
        f"nav page {md} was not built (expected {html_path(md, directory_urls)})"
        for md in nav_files
        if not (site_dir / html_path(md, directory_urls)).is_file()
    ]


def _pages(site_dir: Path, excluded: list[str]) -> Iterator[tuple[Path, str]]:
    """Every built HTML page with its site-relative path, except those under an excluded prefix."""
    for page in sorted(site_dir.rglob("*.html")):
        rel = page.relative_to(site_dir).as_posix()
        if not is_excluded(rel, excluded):
            yield page, rel


def check_redirects(
    site_dir: Path,
    docs_dir: Path,
    blob_prefix: str,
    redirect_maps: dict[str, str],
    directory_urls: bool,
) -> list[str]:
    """Redirect targets must exist, anchors included; external ones only point into this repo's docs."""
    errors: list[str] = []
    for old, target in redirect_maps.items():
        if re.match(r"^[a-z][a-z0-9+.-]*://", target):
            if not target.startswith(blob_prefix):
                errors.append(f"redirect {old}: external target {target} is not in the repository")
            elif not (docs_dir / target.removeprefix(blob_prefix)).exists():
                errors.append(f"redirect {old}: {target} does not exist in the repository")
            continue
        path, _, anchor = target.partition("#")
        page = site_dir / html_path(path, directory_urls)
        if not page.is_file():
            errors.append(f"redirect {old}: target page {path} was not built")
        elif anchor and not re.search(
            rf"""\b(?:id|name)\s*=\s*["']{re.escape(anchor)}["']""", _read(page)
        ):
            errors.append(f"redirect {old}: anchor #{anchor} not found in {path}")
    return errors


class _AttrCollector(HTMLParser):
    """Collects the values of the named attributes from every start tag."""

    def __init__(self, names: tuple[str, ...]) -> None:
        super().__init__()
        self._names = names
        self.values: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        self.values.extend(v for k, v in attrs if k in self._names and v)


def _collect(page: Path, *names: str) -> list[str]:
    collector = _AttrCollector(names)
    collector.feed(_read(page))
    return collector.values


def check_links(site_dir: Path, excluded_prefixes: list[str]) -> list[str]:
    """Relative links must resolve inside the site and never into excluded paths."""
    errors: list[str] = []
    for page, rel in _pages(site_dir, excluded_prefixes):
        for link in _collect(page, "href", "src"):
            if re.match(r"^(?:[a-zA-Z][a-zA-Z0-9+.-]*:|//)", link):
                continue
            path = unquote(re.split(r"[?#]", link, maxsplit=1)[0])
            if not path:
                continue
            base = "" if path.startswith("/") else posixpath.dirname(rel)
            target = posixpath.normpath(posixpath.join(base, path.lstrip("/")))
            # normpath drops a trailing slash, so test the directory form of the target too.
            if is_excluded(target, excluded_prefixes):
                errors.append(f"{rel}: link {link} points into an excluded path")
                continue
            candidate = site_dir / target
            if not (candidate.is_file() or (candidate / "index.html").is_file()):
                errors.append(f"{rel}: broken link {link}")
    return errors


def check_orphans(site_dir: Path, nav_files: list[str], directory_urls: bool) -> list[str]:
    """Every built page must be reachable through the nav (redirect stubs and 404 excepted)."""
    expected = {html_path(md, directory_urls) for md in nav_files}
    errors: list[str] = []
    for page, rel in _pages(site_dir, []):
        if rel == "404.html" or rel.startswith(NON_PAGE_DIRS) or rel in expected:
            continue
        if '<meta http-equiv="refresh"' in _read(page):
            continue
        errors.append(f"orphan page {rel} is not in the nav")
    return errors


def check_weight(site_dir: Path, limit: int = WEIGHT_LIMIT) -> list[str]:
    """The theme's own CSS and JS must stay under the byte budget."""
    theme = site_dir / "assets" / "theme"
    total = sum(
        f.stat().st_size for pattern in ("*.css", "*.js") for f in theme.rglob(pattern) if f.is_file()
    )
    if total > limit:
        return [f"theme CSS+JS weigh {total} bytes, limit is {limit}"]
    return []


def check_cname(site_dir: Path, domain: str = CUSTOM_DOMAIN) -> list[str]:
    """site/CNAME (copied from docs/CNAME) must name the domain the site is live on."""
    cname = site_dir / "CNAME"
    if not cname.is_file():
        return [f"CNAME is missing from the site, expected {domain}"]
    found = _read(cname).strip()
    return [] if found == domain else [f"CNAME is {found!r}, expected {domain!r}"]


def check_unique_ids(site_dir: Path, excluded_prefixes: list[str]) -> list[str]:
    """No page may carry the same id twice (content headings must not clash with theme ids)."""
    errors: list[str] = []
    for page, rel in _pages(site_dir, excluded_prefixes):
        seen: set[str] = set()
        for id_ in _collect(page, "id"):
            if id_ in seen:
                errors.append(f"{rel}: duplicate id {id_!r}")
            seen.add(id_)
    return errors


class _CurrentCounter(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.workspaces = 0
        self.tabs = 0
        self._tabs_tag = ""
        self._depth = 0

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        a = {k: v or "" for k, v in attrs}
        if self._depth:
            if tag == self._tabs_tag:
                self._depth += 1
        elif a.get("id") == "cw-tabs":
            self._tabs_tag, self._depth = tag, 1
        classes = a.get("class", "").split()
        if tag == "a" and "ws" in classes and "aria-current" in a:
            self.workspaces += 1
        if tag == "a" and self._depth and "tab" in classes and a.get("aria-current") == "page":
            self.tabs += 1

    def handle_endtag(self, tag: str) -> None:
        if self._depth and tag == self._tabs_tag:
            self._depth -= 1


def check_current(site_dir: Path, nav_files: list[str], directory_urls: bool) -> list[str]:
    """Each nav page marks exactly one workspace and exactly one tab as current."""
    errors: list[str] = []
    for md in nav_files:
        page = site_dir / html_path(md, directory_urls)
        if not page.is_file():
            continue
        counter = _CurrentCounter()
        counter.feed(_read(page))
        if counter.workspaces != 1:
            errors.append(f"{md}: {counter.workspaces} current workspace links, expected 1")
        if counter.tabs != 1:
            errors.append(f"{md}: {counter.tabs} current tabs, expected 1")
    return errors


def _nav_files(nav: object) -> list[str]:
    if isinstance(nav, str):
        return [nav] if nav.endswith(".md") else []
    if isinstance(nav, list):
        return [f for item in nav for f in _nav_files(item)]
    if isinstance(nav, dict):
        return [f for item in nav.values() for f in _nav_files(item)]
    return []


def main(argv: list[str]) -> int:
    from mkdocs.config import load_config

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="mkdocs.yml")
    parser.add_argument("--site", default="site")
    parser.add_argument("--no-directory-urls", action="store_true")
    args = parser.parse_args(argv)

    config = load_config(args.config)
    directory_urls = config["use_directory_urls"] and not args.no_directory_urls
    site_dir = Path(args.site)
    docs_dir = Path(config["docs_dir"])
    blob_prefix = config["repo_url"].rstrip("/") + "/blob/master/docs/"
    excluded = excluded_prefixes([p.pattern for p in config["exclude_docs"].patterns])
    nav_files = _nav_files(config["nav"])
    redirects = config["plugins"].get("redirects")
    redirect_maps = dict(redirects.config.get("redirect_maps", {})) if redirects else {}

    errors = check_nav_pages(site_dir, nav_files, directory_urls)
    errors += check_redirects(site_dir, docs_dir, blob_prefix, redirect_maps, directory_urls)
    errors += check_links(site_dir, excluded)
    errors += check_orphans(site_dir, nav_files, directory_urls)
    errors += check_cname(site_dir)
    errors += check_current(site_dir, nav_files, directory_urls)
    errors += check_weight(site_dir)
    errors += check_unique_ids(site_dir, excluded)
    for error in errors:
        print(error)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
