#!/usr/bin/env python3
"""Zips a PortMaster port folder for a device: the launcher and the port
directory at the zip's root, with Unix permissions (the launcher and the
binary executable), so it unzips into ports/ as it should from Windows too.
Metadata files named after the zip go inside the port directory, where
PortMaster puts them on install (the testing zip's installed layout).

    python make_zip.py <port folder> <launcher> <port dir> <zip> [metadata...]
"""
import os
import sys
import zipfile


def add(z, path, arc, mode):
    info = zipfile.ZipInfo.from_file(path, arc)
    info.external_attr = (mode << 16) | (0x10 if os.path.isdir(path) else 0)
    info.compress_type = zipfile.ZIP_DEFLATED
    if os.path.isdir(path):
        z.writestr(info, b"")
    else:
        with open(path, "rb") as f:
            z.writestr(info, f.read())


def main():
    port, launcher, portdir, zpath = sys.argv[1:5]
    metadata = sys.argv[5:]
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        add(z, os.path.join(port, launcher), launcher, 0o100755)
        root = os.path.join(port, portdir)
        for d, dirs, files in os.walk(root):
            dirs.sort()
            rel = os.path.relpath(d, port).replace(os.sep, "/")
            add(z, d, rel + "/", 0o040755)
            if rel == portdir:
                for f in metadata:
                    add(z, os.path.join(port, f), rel + "/" + f, 0o100644)
            for f in sorted(files):
                mode = 0o100755 if f.endswith((".armhf", ".aarch64")) or f == "patchscript" else 0o100644
                add(z, os.path.join(d, f), rel + "/" + f, mode)
    print(zpath, os.path.getsize(zpath), "bytes")


if __name__ == "__main__":
    main()
