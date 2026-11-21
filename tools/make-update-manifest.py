#!/usr/bin/env python3
"""Generate a desktop instance update manifest from explicit release files."""

import argparse
import hashlib
import json
import os
import re
import shutil
import tempfile
from uuid import uuid4
from pathlib import Path, PurePosixPath
from urllib.parse import quote, unquote, urljoin


def publish(archive, install_id, root, manifest):
    archive = archive.resolve()
    archive.mkdir(parents=True, exist_ok=True)
    lock = archive / ".updates-publish-lock"
    lock.mkdir()
    try:
        index_path = archive / "updates.json"
        index = (json.loads(index_path.read_text(encoding="utf-8")) if index_path.exists()
                 else {"formatVersion": 1, "updates": {}})
        if index.get("formatVersion") != 1 or not isinstance(index.get("updates"), dict):
            raise ValueError("Existing updates.json has an invalid format")

        files = {}
        update_number = 1
        previous = index["updates"].get(install_id)
        if previous:
            old_path = (archive / unquote(previous)).resolve()
            if not old_path.is_relative_to(archive):
                raise ValueError("Previous manifest is outside the archive directory")
            old = json.loads(old_path.read_text(encoding="utf-8"))
            prior_number = old.get("updateNumber", 0)
            if type(prior_number) is not int or not 0 <= prior_number < 2**31 - 1:
                raise ValueError("Invalid previous update number")
            update_number = prior_number + 1
            for entry in old["files"]:
                # Root-relative links keep earlier payloads available to clients that skipped a release.
                entry["url"] = urljoin("/archive/" + previous, entry["url"])
                files[entry["path"].casefold()] = entry

        release = "updates/" + install_id + "/" + uuid4().hex
        destination = archive / release
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=archive, prefix=".update-staging-") as workspace:
            staging = Path(workspace) / "release"
            staging.mkdir()
            for entry in manifest["files"]:
                path = entry["path"]
                key = path.casefold()
                if key in files and files[key]["path"] != path:
                    raise ValueError(f"File casing changed: {path}")
                payload = staging / "files" / path
                payload.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(root / path, payload)
                with payload.open("rb") as stream:
                    checksum = hashlib.sha256()
                    for block in iter(lambda: stream.read(65536), b""):
                        checksum.update(block)
                if payload.stat().st_size != entry["size"] or checksum.hexdigest() != entry["sha256"]:
                    raise ValueError(f"Source changed while copying: {path}")
                files[key] = entry

            for key in files:
                parts = key.split("/")
                if any("/".join(parts[:length]) in files for length in range(1, len(parts))):
                    raise ValueError(f"File and directory conflict: {files[key]['path']}")
            manifest["installationId"] = install_id
            manifest["updateNumber"] = update_number
            manifest["files"] = [files[key] for key in sorted(files)]
            manifest_text = json.dumps(manifest, indent=2) + "\n"
            if len(files) > 5000 or len(manifest_text.encode()) > 2 * 1024 * 1024:
                raise ValueError("Manifest exceeds the launcher limits")
            (staging / "latest.json").write_text(manifest_text, encoding="utf-8")
            staging.rename(destination)
            index["updates"][install_id] = quote(release + "/latest.json", safe="/")
            temporary_index = Path(workspace) / "updates.json"
            temporary_index.write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8")
            os.replace(temporary_index, index_path)
        print(index_path)
    finally:
        lock.rmdir()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", help="Version label for the new release")
    parser.add_argument("directory", type=Path, help="Local directory containing the release files")
    parser.add_argument("files", nargs="+", help="Game files, relative to the release directory")
    parser.add_argument("--url-prefix", default="files/", help="Absolute or manifest-relative payload directory")
    parser.add_argument("--publish-dir", type=Path, help="Website's archive directory or local upload staging directory")
    parser.add_argument("--install-id", help="Existing installation ID, for example 2.2/2.2091")
    args = parser.parse_args()
    if (args.publish_dir is None) != (args.install_id is None):
        parser.error("use --publish-dir and --install-id together")
    if args.publish_dir:
        if not re.fullmatch(r"[a-zA-Z0-9_.-]+/[a-zA-Z0-9_.-]+", args.install_id) or any(
            part in (".", "..") or part.endswith(".") for part in args.install_id.split("/")
        ):
            parser.error("install ID must be a safe category/version path")
        if args.url_prefix != "files/":
            parser.error("publish mode manages the URL prefix automatically")

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
                or path.resolve() != path or not path.is_file() or name.casefold() in seen
                or re.search(r'[\\:<>"|?*\x00-\x1f]', name) or len(name) > 512
                or any(part.startswith(" ") or part.endswith((" ", ".")) for part in relative.parts)
                or any(part.lower().split(".")[0] in {"con", "prn", "aux", "nul"}
                       or re.fullmatch(r"(com|lpt)[0-9¹²³]", part.lower().split(".")[0])
                       for part in relative.parts)):
            parser.error(f"not a unique regular file inside the release directory: {name}")
        leaf = relative.name.lower()
        first = relative.parts[0].lower()
        if (leaf == "instance.json" or leaf.startswith(("ccgamemanager", "cclocallevels", "ccbetterinfo"))
                or first.startswith(".gdlauncher-") or first in ("geode-backups", "trashed-levels")
                or (first == "geode" and (len(relative.parts) < 3 or relative.parts[1].lower() != "mods"))):
            parser.error(f"file belongs to a save or launcher data: {name}")
        if path.stat().st_size > 8 * 1024**3:
            parser.error(f"file exceeds the launcher download limit: {name}")
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

    manifest = {"formatVersion": 1, "version": version, "files": entries}
    if args.publish_dir:
        try:
            publish(args.publish_dir, args.install_id, root, manifest)
        except (OSError, ValueError, KeyError) as error:
            parser.error(str(error))
    else:
        print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
