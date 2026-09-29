"""Shared helpers for reading the GameMaker project (.yy files are JSON with trailing commas)."""
import glob
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
PORT_ROOT = os.path.dirname(HERE)
GM_ROOT = os.environ.get("GM_PROJECT", os.path.join(os.path.dirname(PORT_ROOT), "disaster2d-oss-main"))

_TRAILING_COMMA = re.compile(r",(\s*[}\]])")


def load_yy(path):
    with open(path, encoding="utf-8") as f:
        return json.loads(_TRAILING_COMMA.sub(r"\1", f.read()))


def resource_yy(kind, folder):
    """The .yy inside a resource folder; folder and file names do not always match."""
    base = os.path.join(GM_ROOT, kind, folder)
    exact = os.path.join(base, folder + ".yy")
    if os.path.exists(exact):
        return exact
    found = glob.glob(os.path.join(base, "*.yy"))
    return found[0] if found else None


def iter_resources(kind):
    """Yield (name, folder_path, yy_dict) for every resource of a kind, sorted by name."""
    out = []
    root = os.path.join(GM_ROOT, kind)
    for folder in os.listdir(root):
        yy = resource_yy(kind, folder)
        if not yy:
            continue
        data = load_yy(yy)
        out.append((data["name"], os.path.dirname(yy), data))
    out.sort(key=lambda t: t[0])
    return out


def c_ident(name):
    return re.sub(r"[^A-Za-z0-9_]", "_", name).upper()


def write_if_changed(path, data):
    """Avoid touching unchanged outputs so make does not rebuild everything."""
    mode = "wb" if isinstance(data, (bytes, bytearray)) else "w"
    if os.path.exists(path):
        with open(path, "rb" if mode == "wb" else "r", **({} if mode == "wb" else {"encoding": "utf-8"})) as f:
            if f.read() == data:
                return False
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, mode, **({} if mode == "wb" else {"encoding": "utf-8", "newline": "\n"})) as f:
        f.write(data)
    return True
