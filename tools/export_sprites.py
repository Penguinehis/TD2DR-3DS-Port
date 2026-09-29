"""Convert every GameMaker sprite into 3DS texture sheets + a binary index.

Each sprite gets one of four modes (see classify()):
  UNUSED     referenced nowhere; no data, id kept so ids stay stable
  MASK       only used by invisible objects (collision shapes); 1-bit mask, no texture
  TILEMAP    screen-sized or bigger (level art, big backgrounds); stored as 16x16 tiles,
             deduplicated with flips, drawn tile by tile
  NORMAL     everything else; cropped frames packed sprite by sprite into sheets, so the
             runtime can load/evict sheets lazily and a sprite touches few sheets

Outputs
  gfx/<group>_<n>.png/.t3s     sprite sheets    -> romfs/gfx/<group>_<n>.t3x
  gfx/<group>_t<n>.png/.t3s    tile sheets      -> romfs/gfx/<group>_t<n>.t3x
  romfs/data/sprites.bin       index (layout documented in source/sprite.c)
  romfs/data/masks.bin         precise collision masks
  source/gen/sprites.h         SPR_* and TG_* ids
"""
import hashlib
import os
import re
import struct
import sys

from PIL import Image

from gmproject import GM_ROOT, PORT_ROOT, c_ident, iter_resources, write_if_changed

# 512x512 16-bit sheets (0.5 MB each): small enough that the Old 3DS texture cache can keep
# what a frame needs, even in the biggest levels.
MAX_TEX = 512
PAD = 1
TILE = 16
TILES_PER_ROW = MAX_TEX // TILE
TILES_PER_SHEET = TILES_PER_ROW * TILES_PER_ROW
TILEMAP_MIN_AREA = 400 * 240   # a frame at least one 3DS screen big becomes a tilemap
# Full-screen GUI overlays drawn every frame: one quad from a normal sheet instead of ~500 tiles
FORCE_NORMAL = {"spr_screenoverlay", "spr_screenoverlay2", "spr_hidegui", "spr_attackgui"}

MODE_UNUSED, MODE_MASK, MODE_TILEMAP, MODE_NORMAL = 0, 1, 2, 3
KIND_PRECISE, KIND_PRECISE_PER_FRAME = 0, 4
FLIP_H, FLIP_V = 1 << 30, 1 << 31
EMPTY_CELL = 0xFFFFFFFF


# ---------------------------------------------------------------- classification

def gather_references(sprite_names):
    """Where each sprite is referenced: in code, in rooms, as a visible/invisible object sprite."""
    word = re.compile(r"\b[A-Za-z_][A-Za-z0-9_.]*\b")
    names = set(sprite_names)
    in_code, in_room, obj_visible, obj_invisible = set(), set(), set(), set()

    for kind in ("objects", "scripts", "rooms"):
        for dirpath, _, files in os.walk(os.path.join(GM_ROOT, kind)):
            for fn in files:
                if fn.endswith(".gml"):
                    with open(os.path.join(dirpath, fn), encoding="utf-8", errors="ignore") as f:
                        in_code.update(names.intersection(word.findall(f.read())))

    for name, _, d in iter_resources("objects"):
        for key in ("spriteId", "spriteMaskId"):
            ref = d.get(key)
            if ref:
                visible = d.get("visible", True) and key == "spriteId"
                (obj_visible if visible else obj_invisible).add(ref["name"])

    for name, _, d in iter_resources("rooms"):
        stack = list(d.get("layers", []))
        while stack:
            layer = stack.pop()
            stack.extend(layer.get("layers", []))
            ref = layer.get("spriteId")
            if ref:
                in_room.add(ref["name"])
            for asset in layer.get("assets", []) or []:
                ref = asset.get("spriteId")
                if ref:
                    in_room.add(ref["name"])
    return in_code, in_room, obj_visible, obj_invisible


def classify(name, d, frame_imgs, refs):
    in_code, in_room, obj_visible, obj_invisible = refs
    drawn = name in in_code or name in in_room or name in obj_visible
    if not drawn:
        return MODE_MASK if name in obj_invisible else MODE_UNUSED
    if d["width"] * d["height"] >= TILEMAP_MIN_AREA and name not in FORCE_NORMAL:
        return MODE_TILEMAP
    return MODE_NORMAL


# ---------------------------------------------------------------- normal sprites

class Sheets:
    """Shelf packer that only ever fills the newest sheet, keeping each sprite's frames together."""

    def __init__(self):
        self.sheets = []  # dict(img, shelves, y, w)

    def _new(self):
        self.sheets.append({"img": Image.new("RGBA", (MAX_TEX, MAX_TEX)), "shelves": [], "y": 0, "w": 0})

    def place(self, img):
        w, h = img.size[0] + 2 * PAD, img.size[1] + 2 * PAD
        if not self.sheets:
            self._new()
        for attempt in range(2):
            s = self.sheets[-1]
            for shelf in s["shelves"]:
                if h <= shelf[1] and shelf[2] + w <= MAX_TEX:
                    x, y = shelf[2], shelf[0]
                    shelf[2] += w
                    return self._put(s, img, x, y)
            if s["y"] + h <= MAX_TEX:
                s["shelves"].append([s["y"], h, w])
                x, y = 0, s["y"]
                s["y"] += h
                return self._put(s, img, x, y)
            self._new()
        raise RuntimeError("piece larger than a sheet")

    def _put(self, s, img, x, y):
        s["img"].paste(img, (x + PAD, y + PAD))
        s["w"] = max(s["w"], x + img.size[0] + 2 * PAD)
        return len(self.sheets) - 1, x + PAD, y + PAD

    def finished(self):
        for s in self.sheets:
            yield s["img"].crop((0, 0, pow2(s["w"]), pow2(s["y"])))


def sheet_format(img):
    """16-bit texture format for a sheet: RGBA5551 when the alpha is (almost) only fully
    opaque or fully transparent (stray soft pixels are thresholded), RGBA4444 when the sheet
    really uses soft alpha (fog, glows, overlays)."""
    alpha = img.getchannel("A")
    hist = alpha.histogram()
    soft = sum(hist[1:255])
    visible = sum(hist[1:])
    if visible == 0 or soft <= visible * 0.01:
        if soft:
            img = img.copy()
            img.putalpha(alpha.point(lambda a: 255 if a >= 128 else 0))
        return "rgba5551", img
    return "rgba4444", img


def pow2(v):
    n = 8
    while n < v:
        n <<= 1
    return n


def split_frame(img):
    """Crop to visible pixels, split into <=MAX_TEX pieces: [(img, dx, dy)]."""
    bbox = img.getchannel("A").getbbox()
    if bbox is None:
        return []
    step = MAX_TEX - 2 * PAD
    left, top, right, bottom = bbox
    out = []
    for y in range(top, bottom, step):
        for x in range(left, right, step):
            out.append((img.crop((x, y, min(x + step, right), min(y + step, bottom))), x, y))
    return out


# ---------------------------------------------------------------- tilemaps

class Tileset:
    def __init__(self):
        self.tiles = []
        self.lookup = {}  # bytes of any orientation -> cell value

    def cell(self, tile):
        if tile.getchannel("A").getbbox() is None:
            return EMPTY_CELL
        key = tile.tobytes()
        hit = self.lookup.get(key)
        if hit is not None:
            return hit
        idx = len(self.tiles)
        self.tiles.append(tile)
        variants = (
            (tile, 0),
            (tile.transpose(Image.FLIP_LEFT_RIGHT), FLIP_H),
            (tile.transpose(Image.FLIP_TOP_BOTTOM), FLIP_V),
            (tile.transpose(Image.ROTATE_180), FLIP_H | FLIP_V),
        )
        for img, flags in variants:
            self.lookup.setdefault(img.tobytes(), idx | flags)
        return idx

    def sheets(self):
        for start in range(0, len(self.tiles), TILES_PER_SHEET):
            chunk = self.tiles[start:start + TILES_PER_SHEET]
            rows = (len(chunk) + TILES_PER_ROW - 1) // TILES_PER_ROW
            img = Image.new("RGBA", (MAX_TEX, pow2(rows * TILE)))
            for i, t in enumerate(chunk):
                img.paste(t, ((i % TILES_PER_ROW) * TILE, (i // TILES_PER_ROW) * TILE))
            yield img


def tilemap_frame(img, tileset):
    """Cells covering the visible area of a frame: (x0, y0, cols, rows, cells)."""
    bbox = img.getchannel("A").getbbox()
    if bbox is None:
        return 0, 0, 0, 0, []
    x0, y0 = bbox[0] // TILE * TILE, bbox[1] // TILE * TILE
    cols = (bbox[2] - x0 + TILE - 1) // TILE
    rows = (bbox[3] - y0 + TILE - 1) // TILE
    cells = []
    for r in range(rows):
        for c in range(cols):
            x, y = x0 + c * TILE, y0 + r * TILE
            tile = img.crop((x, y, x + TILE, y + TILE))  # crop pads outside with transparency
            cells.append(tileset.cell(tile))
    return x0, y0, cols, rows, cells


# ---------------------------------------------------------------- masks

def build_mask(d, frame_imgs):
    kind = d.get("collisionKind", 1)
    if kind not in (KIND_PRECISE, KIND_PRECISE_PER_FRAME) or not frame_imgs:
        return None
    tol = d.get("collisionTolerance", 0)
    w, h = d["width"], d["height"]
    per_frame = kind == KIND_PRECISE_PER_FRAME
    groups = [[im] for im in frame_imgs] if per_frame else [frame_imgs]
    blobs = []
    for imgs in groups:
        acc = Image.new("L", (w, h), 0)
        for im in imgs:
            a = im.getchannel("A").point(lambda v: 255 if v > tol else 0)
            acc.paste(255, (0, 0), a.crop((0, 0, w, h)))
        # PIL "1" mode packs MSB-first per row; repack LSB-first, row-major, no row padding
        px = acc.tobytes()
        bits = bytearray((w * h + 7) // 8)
        for i, v in enumerate(px):
            if v:
                bits[i >> 3] |= 1 << (i & 7)
        blobs.append(bytes(bits))
    return per_frame, w, h, blobs


# ---------------------------------------------------------------- main

def main():
    sprites = iter_resources("sprites")
    refs = gather_references([n for n, _, _ in sprites])
    groups = sorted({d["textureGroupId"]["name"] for _, _, d in sprites})
    gidx = {g: i for i, g in enumerate(groups)}
    sheets = {g: Sheets() for g in groups}
    tilesets = {g: Tileset() for g in groups}

    sprite_rec, frames_tab, pieces_tab, maps_tab, cells_tab, masks = [], [], [], [], [], []
    mode_count = [0, 0, 0, 0]
    piece_cache = {g: {} for g in groups}

    for sid, (name, folder, d) in enumerate(sprites):
        g = d["textureGroupId"]["name"]
        frame_imgs = []
        for fr in d["frames"]:
            p = os.path.join(folder, fr["name"] + ".png")
            frame_imgs.append(Image.open(p).convert("RGBA") if os.path.exists(p)
                              else Image.new("RGBA", (d["width"], d["height"])))
        mode = classify(name, d, frame_imgs, refs)
        mode_count[mode] += 1
        first_frame = len(frames_tab)

        if mode == MODE_NORMAL:
            for img in frame_imgs:
                first_piece = len(pieces_tab)
                for piece, dx, dy in split_frame(img):
                    key = hashlib.sha1(piece.tobytes()).digest() + struct.pack("<HH", *piece.size)
                    loc = piece_cache[g].get(key)
                    if loc is None:
                        loc = piece_cache[g][key] = sheets[g].place(piece)
                    sheet, x, y = loc
                    pieces_tab.append(struct.pack("<BHHHHhh", sheet, x, y, piece.size[0], piece.size[1], dx, dy))
                frames_tab.append(struct.pack("<IH", first_piece, len(pieces_tab) - first_piece))
        elif mode == MODE_TILEMAP:
            for img in frame_imgs:
                x0, y0, cols, rows, cells = tilemap_frame(img, tilesets[g])
                maps_tab.append(struct.pack("<hhHHI", x0, y0, cols, rows, len(cells_tab)))
                cells_tab.extend(cells)
                frames_tab.append(struct.pack("<IH", len(maps_tab) - 1, 0))
        else:
            for _ in frame_imgs:
                frames_tab.append(struct.pack("<IH", 0, 0))

        m = build_mask(d, frame_imgs) if mode != MODE_UNUSED else None
        if m:
            masks.append((sid,) + m)

        seq = d.get("sequence", {})
        sprite_rec.append(struct.pack(
            "<BHHHhhhhhhBBfHI",
            mode, gidx[g], d["width"], d["height"],
            int(seq.get("xorigin", 0)), int(seq.get("yorigin", 0)),
            d.get("bbox_left", 0), d.get("bbox_top", 0), d.get("bbox_right", 0), d.get("bbox_bottom", 0),
            d.get("collisionKind", 1), int(seq.get("playbackSpeedType", 0)),
            float(seq.get("playbackSpeed", 30.0)), len(frame_imgs), first_frame))
        print(f"\r{sid + 1}/{len(sprites)} {name[:40]:40}", end="", file=sys.stderr)
    print(file=sys.stderr)

    # sheets
    gfx_dir = os.path.join(PORT_ROOT, "gfx")
    for f in os.listdir(gfx_dir):
        if f.endswith((".png", ".t3s")):
            os.remove(os.path.join(gfx_dir, f))
    report, group_hdr = [], bytearray()
    for g in groups:
        base = g.lower()
        sprite_imgs = list(sheets[g].finished())
        tile_imgs = list(tilesets[g].sheets())
        size = 0
        for prefix, imgs in ((f"{base}_", sprite_imgs), (f"{base}_t", tile_imgs)):
            for i, img in enumerate(imgs):
                fmt, img = sheet_format(img)
                img.save(os.path.join(gfx_dir, f"{prefix}{i}.png"), compress_level=1)
                write_if_changed(os.path.join(gfx_dir, f"{prefix}{i}.t3s"), f"-f {fmt} -z auto\n{prefix}{i}.png\n")
                size += img.size[0] * img.size[1] * 2
        group_hdr += struct.pack("<32sBB", base.encode()[:31], len(sprite_imgs), len(tile_imgs))
        report.append((g, len(sprite_imgs), len(tile_imgs), len(tilesets[g].tiles), size))

    # sprites.bin
    out = bytearray(b"SPR2")
    out += struct.pack("<HHIIII", len(sprites), len(groups), len(frames_tab), len(pieces_tab), len(maps_tab), len(cells_tab))
    out += group_hdr
    out += b"".join(sprite_rec) + b"".join(frames_tab) + b"".join(pieces_tab) + b"".join(maps_tab)
    out += struct.pack(f"<{len(cells_tab)}I", *cells_tab)
    write_if_changed(os.path.join(PORT_ROOT, "romfs", "data", "sprites.bin"), bytes(out))

    mout = bytearray(b"MSK1") + struct.pack("<H", len(masks))
    for sid, per_frame, w, h, blobs in masks:
        mout += struct.pack("<HBHHH", sid, per_frame, w, h, len(blobs)) + b"".join(blobs)
    write_if_changed(os.path.join(PORT_ROOT, "romfs", "data", "masks.bin"), bytes(mout))

    lines = ["// Generated by tools/export_sprites.py - do not edit.", "#pragma once", ""]
    lines += [f"#define TG_{c_ident(g)} {i}" for i, g in enumerate(groups)] + [f"#define TG_COUNT {len(groups)}", ""]
    lines += [f"#define {c_ident(n)} {i}" for i, (n, _, _) in enumerate(sprites)] + [f"#define SPR_COUNT {len(sprites)}"]
    write_if_changed(os.path.join(PORT_ROOT, "source", "gen", "sprites.h"), "\n".join(lines) + "\n")

    print(f"{len(sprites)} sprites: {mode_count[MODE_NORMAL]} normal, {mode_count[MODE_TILEMAP]} tilemap, "
          f"{mode_count[MODE_MASK]} mask-only, {mode_count[MODE_UNUSED]} unused; {len(masks)} masks")
    print(f"sprites.bin {len(out) / 1048576:.1f} MB, masks.bin {len(mout) / 1048576:.1f} MB")
    print(f"{'texture group':20} spr.sheets tile.sheets tiles   MB")
    for g, ns, nt, tiles, size in sorted(report, key=lambda r: -r[4]):
        print(f"{g:20} {ns:10} {nt:11} {tiles:5} {size / 1048576:6.1f}")
    print(f"{'TOTAL':20} {sum(r[1] for r in report):10} {sum(r[2] for r in report):11}       {sum(r[4] for r in report) / 1048576:6.1f}")


if __name__ == "__main__":
    main()
