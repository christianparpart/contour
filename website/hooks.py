"""MkDocs hooks: expose the latest published release to the theme."""

import re
import xml.etree.ElementTree as ET
from pathlib import Path

_VERSION = re.compile(r"^\d+\.\d+\.\d+$")


def latest_release(metainfo_xml: str) -> str:
    """Returns the version of the first <release> that is numbered x.y.z and dated, or ""."""
    root = ET.fromstring(metainfo_xml)
    for release in root.iter("release"):
        version = release.get("version", "")
        if _VERSION.match(version) and release.get("date"):
            return version
    return ""


def on_config(config):
    """Sets config.extra["latest_release"] from the metainfo.xml beside mkdocs.yml."""
    metainfo = Path(config.config_file_path).parent / "metainfo.xml"
    config.extra["latest_release"] = latest_release(metainfo.read_text(encoding="utf-8"))
    return config
