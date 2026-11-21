#!/usr/bin/env python3
"""Generate a desktop instance update manifest from explicit release files."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
from urllib.parse import quote


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", help="Version label for the new release")
    parser.add_argument("directory", type=Path, help="Local directory containing the release files")
    parser.add_argument("files", nargs="+", help="Game files, relative to the release directory")
    parser.add_argument("--url-prefix", default="files/", help="Absolute or manifest-relative payload directory")
    args = parser.parse_args()

    version = " ".join(args.version.split())
    if not version or len(version) > 100:
        parser.error("version must contain 1–100 characters")
    root = args.directory.resolve()
    if not root.is_dir():
        parser.error("directory does not exist")
    seen = set()
    entries = []
    for name in sorted(args.files):
        relative = PurePosixPath(name)
        path = root.joinpath(*relative.parts)
        if (relative.is_absolute() or str(relative) != name or ".." in relative.parts
                or path.resolve() != path or not path.is_file() or name.casefold() in seen):
            parser.error(f"not a unique regular file inside the release directory: {name}")
        seen.add(name.casefold())
        with path.open("rb") as payload:
            checksum = hashlib.sha256()
            for block in iter(lambda: payload.read(65536), b""):
                checksum.update(block)
        entries.append({
            "path": name,
            "size": path.stat().st_size,
            "sha256": checksum.hexdigest(),
            "url": args.url_prefix.rstrip("/") + "/" + quote(name, safe="/"),
        })

    print(json.dumps({"formatVersion": 1, "version": version, "files": entries}, indent=2))


if __name__ == "__main__":
    main()
