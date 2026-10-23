# PlatformIO pre-script: compresses web/index.html into data/index.html.gz,
# so "Upload Filesystem Image" always ships the current web UI.
import gzip
import os

Import("env")  # noqa: F821 (provided by PlatformIO)

project = env.subst("$PROJECT_DIR")  # noqa: F821
src = os.path.join(project, "..", "_arduino", "camper_temp", "web", "index.html")
dst = os.path.join(project, "..", "_arduino", "camper_temp", "data", "index.html.gz")

with open(src, "rb") as f:
    html = f.read()
# mtime=0 keeps the output identical for identical input (no git noise)
data = gzip.compress(html, compresslevel=9, mtime=0)
old = open(dst, "rb").read() if os.path.exists(dst) else b""
if data != old:
    with open(dst, "wb") as f:
        f.write(data)
    print("[web] index.html.gz updated (%d -> %d bytes)" % (len(html), len(data)))
