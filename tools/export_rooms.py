"""Convert GameMaker rooms and object definitions to binary data for the 3DS.

Outputs
  romfs/data/rooms/<room>.bin   one per room (layout documented in source/room.c)
  romfs/data/objects.bin        object table: sprite, mask, parent, flags
  source/gen/objects.h          OBJ_* ids
  source/gen/rooms.h            RM_* ids and the room file name table
  ref/rooms/<room>/*.gml        room + instance creation code, copied for hand-porting

Creation code is GML and is not executed on the 3DS. Instances that have it are flagged
and carry their GameMaker instance name so the hand-ported C can find them.
"""
import os
import re
import shutil
import struct
import sys

from gmproject import GM_ROOT, PORT_ROOT, c_ident, iter_resources, write_if_changed

LAYER_BACKGROUND, LAYER_INSTANCES, LAYER_ASSETS, LAYER_LEVELART = 0, 1, 2, 3


def gml_literal(text, default=None):
    text = text.strip()
    if text in ("true", "false"):
        return text == "true"
    try:
        return float(text)
    except ValueError:
        pass
    # Plain arithmetic such as "- 60" or "214+20"; nothing else is evaluated.
    if re.fullmatch(r"[0-9.+\-*/() \t]+", text):
        try:
            return float(eval(text, {"__builtins__": {}}, {}))
        except (SyntaxError, ZeroDivisionError, TypeError):
            pass
    return default


# Constants that appear in instance creation code
GML_CONSTANTS = {"SOUNDEMT_MOVINGSPIKE": "0", "SOUNDEMT_LCCHAIN": "1", "true": "1", "false": "0"}


def creation_assignments(folder, inst_name):
    """Simple `name = value;` lines of an instance's creation code, as extra properties."""
    path = os.path.join(folder, f"InstanceCreationCode_{inst_name}.gml")
    if not os.path.exists(path):
        return []
    out = []
    for line in open(path, encoding="utf-8-sig", errors="ignore"):
        m = re.match(r"\s*([A-Za-z_]\w*)\s*=\s*([^;]+);?\s*$", line)
        if not m:
            continue
        name, val = m.group(1), m.group(2).strip()
        if val in GML_CONSTANTS:
            val = GML_CONSTANTS[val]
        elif gml_literal(val) is not None:
            v = gml_literal(val)
            val = str(int(v)) if isinstance(v, float) and v.is_integer() else str(v)
        elif not (val.startswith('"') and val.endswith('"')):
            continue  # expressions are ported by hand
        out.append((name, val.strip('"')))
    return out


def split_args(text):
    """Split a GML argument list on top-level commas."""
    out, depth, cur = [], 0, ""
    for ch in text:
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return [a.strip() for a in out]


def parse_creation_code(code, room_name):
    """Pull the level-art splits and the parallax table out of RoomCreationCode.gml.

    scr_level_split(spr, depth [, visible [, yOffset]])
    scr_level_splitl(spr, "Layer" [, visible [, yOffset]])
    global.parallax = [ [ layer_get_id("Layer"), xspd, yspd [, yoff] ], ... ]
    """
    code = re.sub(r"//[^\n]*", "", code)
    splits = []
    for m in re.finditer(r"\bscr_level_split(l?)\s*\((.*?)\)\s*;", code):
        args = split_args(m.group(2))
        entry = {"sprite": args[0], "visible": True, "yoff": 0.0}
        if m.group(1):
            entry["layer"] = args[1].strip('"')
        else:
            entry["depth"] = gml_literal(args[1], 0)
        if len(args) > 2:
            entry["visible"] = bool(gml_literal(args[2], True))
        if len(args) > 3:
            entry["yoff"] = gml_literal(args[3], 0.0)
        splits.append(entry)

    parallax = {}
    m = re.search(r"global\.parallax\s*=\s*\[(.*?)\]\s*;?\s*(?:\n\s*\n|$|scr_|global\.|var |if)", code, re.S)
    if m:
        for row in re.finditer(r"\[\s*layer_get_id\(\s*\"([^\"]+)\"\s*\)\s*,([^\]]*)\]", m.group(1)):
            nums = [gml_literal(v, None) for v in split_args(row.group(2))]
            if None in nums or len(nums) < 2:
                print(f"  {room_name}: could not parse parallax row for {row.group(1)}", file=sys.stderr)
                continue
            parallax[row.group(1)] = (nums[0], nums[1], nums[2] if len(nums) > 2 else None)
    return splits, parallax

INST_VISIBLE_UNSET = 0  # placeholder for future per-instance flags
INST_HAS_CODE = 1


def s(text):
    """GameMaker-style NUL-terminated string."""
    return text.encode("utf-8") + b"\0"


def ref_name(ref):
    return ref["name"] if ref else None


def main():
    sprites = [n for n, _, _ in iter_resources("sprites")]
    spr_id = {n: i for i, n in enumerate(sprites)}
    objects = iter_resources("objects")
    obj_id = {n: i for i, (n, _, _) in enumerate(objects)}
    rooms = iter_resources("rooms")

    def sid(ref):
        return spr_id.get(ref_name(ref), -1) if ref else -1

    def oid(ref):
        return obj_id.get(ref_name(ref), -1) if ref else -1

    # A Draw event (type 8, num 0), own or inherited, replaces GameMaker's default sprite drawing.
    by_name = {n: d for n, _, d in objects}

    def has_draw(name, guard=0):
        d = by_name.get(name)
        if not d or guard > 64:
            return False
        if any(e["eventType"] == 8 and e["eventNum"] == 0 for e in d.get("eventList", [])):
            # a Draw event that only calls draw_self() is the default drawing (obj_spike...)
            path = os.path.join(GM_ROOT, "objects", name, "Draw_0.gml")
            code = open(path, encoding="utf-8-sig").read() if os.path.exists(path) else ""
            code = re.sub(r"\s+", "", re.sub(r"//.*", "", code))
            return code not in ("draw_self();", "draw_self()")
        parent = ref_name(d.get("parentObjectId"))
        return has_draw(parent, guard + 1) if parent else False

    def obj_defaults(name, guard=0):
        d = by_name.get(name)
        if not d or guard > 64:
            return []
        parent = ref_name(d.get("parentObjectId"))
        out = obj_defaults(parent, guard + 1) if parent else []
        for p in d.get("properties", []) or []:
            out.append((p["name"], str(p.get("value", ""))))
        for p in d.get("overriddenProperties", []) or []:
            out.append((p["propertyId"]["name"], str(p.get("value", ""))))
        return out

    # canStuck (Knuckles can cling to the wall) is set in Create_0 of obj_floor_parent and
    # overridden by some children; resolved through the parent chain.
    def can_stuck(name, guard=0):
        d = by_name.get(name)
        if not d or guard > 64:
            return True
        path = os.path.join(GM_ROOT, "objects", name, "Create_0.gml")
        if os.path.exists(path):
            m = re.search(r"^\s*canStuck\s*=\s*(true|false)", open(path, encoding="utf-8-sig").read(), re.M)
            if m:
                return m.group(1) == "true"
        parent = ref_name(d.get("parentObjectId"))
        return can_stuck(parent, guard + 1) if parent else True

    # objects.bin: flags bit0 visible, bit1 solid, bit2 persistent, bit3 custom Draw event,
    # bit4 canStuck == false
    ob = bytearray(b"OBJ1") + struct.pack("<H", len(objects))
    for name, _, d in objects:
        flags = (1 if d.get("visible", True) else 0) | (2 if d.get("solid") else 0) | (4 if d.get("persistent") else 0)
        flags |= 8 if has_draw(name) else 0
        flags |= 16 if not can_stuck(name) else 0
        ob += struct.pack("<hhhB", sid(d.get("spriteId")), sid(d.get("spriteMaskId")), oid(d.get("parentObjectId")), flags)
    write_if_changed(os.path.join(PORT_ROOT, "romfs", "data", "objects.bin"), bytes(ob))

    ref_root = os.path.join(PORT_ROOT, "ref", "rooms")
    total_inst = 0
    for rname, folder, d in rooms:
        rs = d["roomSettings"]
        view = next((v for v in d.get("views", []) if v.get("visible")), None)
        vw, vh = (view["wview"], view["hview"]) if view else (rs["Width"], rs["Height"])

        layers = []
        stack = list(reversed(d.get("layers", [])))
        flat = []
        while stack:  # depth-first, keeps editor order; nested layers are flattened
            layer = stack.pop()
            flat.append(layer)
            stack.extend(reversed(layer.get("layers", [])))

        cc_path = os.path.join(folder, "RoomCreationCode.gml")
        splits, parallax = ([], {})
        if os.path.exists(cc_path):
            with open(cc_path, encoding="utf-8", errors="ignore") as f:
                splits, parallax = parse_creation_code(f.read(), rname)
        depth_of = {layer["name"]: int(layer.get("depth", 0)) for layer in flat}

        for sp in splits:
            spr = spr_id.get(sp["sprite"], -1)
            depth = depth_of.get(sp["layer"], 0) if "layer" in sp else int(sp["depth"])
            if spr < 0 or ("layer" in sp and sp["layer"] not in depth_of):
                print(f"  {rname}: unresolved level split {sp}", file=sys.stderr)
            layers.append(struct.pack("<BiB", LAYER_LEVELART, depth, 1 if sp["visible"] else 0)
                          + s(sp["sprite"]) + struct.pack("<hf", spr, float(sp["yoff"])))

        creation = {c["name"]: i for i, c in enumerate(d.get("instanceCreationOrder", []) or [])}
        for layer in flat:
            t = layer["resourceType"]
            head = struct.pack("<iB", int(layer.get("depth", 0)), 1 if layer.get("visible", True) else 0)
            if t == "GMRBackgroundLayer":
                px = parallax.get(layer["name"])
                body = struct.pack(
                    "<hiiBBBIffBfffB", sid(layer.get("spriteId")), int(layer.get("x", 0)), int(layer.get("y", 0)),
                    1 if layer.get("htiled") else 0, 1 if layer.get("vtiled") else 0, 1 if layer.get("stretch") else 0,
                    layer.get("colour", 0xFFFFFFFF) & 0xFFFFFFFF,
                    float(layer.get("hspeed", 0)), float(layer.get("vspeed", 0)),
                    1 if px else 0, px[0] if px else 0.0, px[1] if px else 0.0,
                    (px[2] or 0.0) if px else 0.0, 1 if px and px[2] is not None else 0)
                layers.append(struct.pack("<B", LAYER_BACKGROUND) + head + s(layer["name"]) + body)
            elif t == "GMRInstanceLayer":
                insts = bytearray()
                for inst in layer.get("instances", []):
                    props = bytearray()
                    # object variable defaults (parents first), then the instance's own values
                    plist = obj_defaults(ref_name(inst.get("objectId")))
                    plist += [(p["propertyId"]["name"], str(p.get("value", ""))) for p in inst.get("properties", []) or []]
                    plist += creation_assignments(folder, inst["name"]) if inst.get("hasCreationCode") else []
                    for pname, pval in plist:
                        props += s(pname) + s(pval)
                    flags = INST_HAS_CODE if inst.get("hasCreationCode") else 0
                    insts += struct.pack(
                        "<hffffffIhB", oid(inst.get("objectId")), float(inst["x"]), float(inst["y"]),
                        float(inst.get("scaleX", 1)), float(inst.get("scaleY", 1)), float(inst.get("rotation", 0)),
                        float(inst.get("imageSpeed", 1)), inst.get("colour", 0xFFFFFFFF) & 0xFFFFFFFF,
                        int(inst.get("imageIndex", 0)), flags)
                    insts += s(inst["name"]) + struct.pack("<HB", creation.get(inst["name"], 0xFFFF),
                                                           len(plist)) + props
                    total_inst += 1
                layers.append(struct.pack("<B", LAYER_INSTANCES) + head + s(layer["name"])
                              + struct.pack("<H", len(layer.get("instances", []))) + insts)
            elif t == "GMRAssetLayer":
                assets = [a for a in layer.get("assets", []) or [] if a.get("resourceType") == "GMRSpriteGraphic"]
                body = bytearray(struct.pack("<H", len(assets)))
                for a in assets:
                    body += struct.pack("<hfffffffI", sid(a.get("spriteId")), float(a["x"]), float(a["y"]),
                                        float(a.get("scaleX", 1)), float(a.get("scaleY", 1)), float(a.get("rotation", 0)),
                                        float(a.get("headPosition", 0)), float(a.get("animationSpeed", 1)),
                                        a.get("colour", 0xFFFFFFFF) & 0xFFFFFFFF)
                layers.append(struct.pack("<B", LAYER_ASSETS) + head + s(layer["name"]) + bytes(body))
            # plain GMRLayer folders carry nothing drawable

        out = bytearray(b"ROM1")
        out += struct.pack("<IIHHH", rs["Width"], rs["Height"], vw, vh, len(layers))
        out += b"".join(layers)
        write_if_changed(os.path.join(PORT_ROOT, "romfs", "data", "rooms", rname + ".bin"), bytes(out))

        # creation code for hand-porting
        dst = os.path.join(ref_root, rname)
        for fn in os.listdir(folder):
            if fn.endswith(".gml"):
                os.makedirs(dst, exist_ok=True)
                shutil.copyfile(os.path.join(folder, fn), os.path.join(dst, fn))

    oh = ["// Generated by tools/export_rooms.py - do not edit.", "#pragma once", ""]
    oh += [f"#define {c_ident(n)} {i}" for i, (n, _, _) in enumerate(objects)] + [f"#define OBJ_COUNT {len(objects)}"]
    write_if_changed(os.path.join(PORT_ROOT, "source", "gen", "objects.h"), "\n".join(oh) + "\n")

    rh = ["// Generated by tools/export_rooms.py - do not edit.", "#pragma once", ""]
    rh += [f"#define {c_ident(n)} {i}" for i, (n, _, _) in enumerate(rooms)] + [f"#define RM_COUNT {len(rooms)}", ""]
    rh += ["static const char *const ROOM_FILES[RM_COUNT] = {"]
    rh += [f'    "romfs:/data/rooms/{n}.bin",' for n, _, _ in rooms] + ["};"]
    write_if_changed(os.path.join(PORT_ROOT, "source", "gen", "rooms.h"), "\n".join(rh) + "\n")

    print(f"{len(rooms)} rooms, {len(objects)} objects, {total_inst} instances")


if __name__ == "__main__":
    main()
