#!/usr/bin/env python3
"""Writes tests/data/sample.fig: a small Figma-format file for the .fig importer tests.

It encodes a kiwi schema and message the way Figma does (field names from Figma's schema), so
the importer's decoding and mapping can be tested without a real Figma export. The message is
zstd-compressed and the schema deflate-compressed, covering both decompressors; the canvas is
zipped with an image like files saved from Figma.

    pip install zstandard && python3 scripts/make-test-fig.py
"""
import io
import os
import struct
import zipfile
import zlib

import zstandard

BOOL, BYTE, INT, UINT, FLOAT, STRING, INT64, UINT64 = range(-1, -9, -1)
ENUM, STRUCT, MESSAGE = 0, 1, 2

# ---------------------------------------------------------------- schema
DEFS = []


def define(name, kind, fields):
    DEFS.append((name, kind, fields))


def T(name):
    return next(i for i, d in enumerate(DEFS) if d[0] == name)


define("GUID", STRUCT, [("sessionID", UINT), ("localID", UINT)])
define("Color", STRUCT, [("r", FLOAT), ("g", FLOAT), ("b", FLOAT), ("a", FLOAT)])
define("Vector", STRUCT, [("x", FLOAT), ("y", FLOAT)])
define("Matrix", STRUCT, [(k, FLOAT) for k in ("m00", "m01", "m02", "m10", "m11", "m12")])
define("NodeType", ENUM, ["DOCUMENT", "CANVAS", "FRAME", "GROUP", "VECTOR", "BOOLEAN_OPERATION", "STAR", "LINE",
                          "ELLIPSE", "RECTANGLE", "REGULAR_POLYGON", "ROUNDED_RECTANGLE", "TEXT", "SLICE", "SYMBOL",
                          "INSTANCE", "STICKY"])
define("PaintType", ENUM, ["SOLID", "GRADIENT_LINEAR", "GRADIENT_RADIAL", "GRADIENT_ANGULAR", "GRADIENT_DIAMOND",
                           "IMAGE"])
define("StackMode", ENUM, ["NONE", "HORIZONTAL", "VERTICAL"])
define("StackSize", ENUM, ["FIXED", "RESIZE_TO_FIT", "RESIZE_TO_FIT_WITH_IMPLICIT_SIZE"])
define("TextAutoResize", ENUM, ["NONE", "WIDTH_AND_HEIGHT", "HEIGHT"])
define("StyleType", ENUM, ["NONE", "FILL", "STROKE", "TEXT", "EFFECT", "EXPORT", "GRID"])
define("EffectType", ENUM, ["INNER_SHADOW", "DROP_SHADOW", "FOREGROUND_BLUR", "BACKGROUND_BLUR"])
define("ComponentPropType", ENUM, ["BOOL", "TEXT", "COLOR", "INSTANCE_SWAP", "VARIANT"])
define("ComponentPropNodeField", ENUM, ["VISIBLE", "TEXT_DATA", "OVERRIDDEN_SYMBOL_ID"])
define("LineType", ENUM, ["PLAIN", "ORDERED_LIST", "UNORDERED_LIST"])
define("ColorStop", STRUCT, [("color", "Color"), ("position", FLOAT)])
define("Image", MESSAGE, [("hash", BYTE, True), ("name", STRING)])
define("Paint", MESSAGE, [("type", "PaintType"), ("color", "Color"), ("opacity", FLOAT), ("visible", BOOL),
                          ("stops", "ColorStop", True), ("transform", "Matrix"), ("image", "Image"),
                          ("imageScaleMode", STRING)])
define("Effect", MESSAGE, [("type", "EffectType"), ("color", "Color"), ("offset", "Vector"), ("radius", FLOAT),
                           ("spread", FLOAT), ("visible", BOOL)])
define("ParentIndex", MESSAGE, [("guid", "GUID"), ("position", STRING)])
define("FontName", MESSAGE, [("family", STRING), ("style", STRING), ("postscript", STRING)])
define("TextLine", MESSAGE, [("lineType", "LineType"), ("indentationLevel", INT)])
define("GUIDPath", MESSAGE, [("guids", "GUID", True)])
define("VectorData", MESSAGE, [("vectorNetworkBlob", UINT), ("normalizedSize", "Vector")])
# TextData and NodeChange refer to each other (style override tables are NodeChanges).
define("TextData", MESSAGE, [("characters", STRING), ("characterStyleIDs", UINT, True),
                             ("styleOverrideTable", "NodeChange", True), ("lines", "TextLine", True)])
define("ComponentPropValue", MESSAGE, [("boolValue", BOOL), ("textValue", "TextData"), ("guidValue", "GUID")])
define("ComponentPropDef", MESSAGE, [("id", "GUID"), ("name", STRING), ("type", "ComponentPropType"),
                                     ("initialValue", "ComponentPropValue")])
define("ComponentPropRef", MESSAGE, [("defID", "GUID"), ("componentPropNodeField", "ComponentPropNodeField"),
                                     ("isDeleted", BOOL)])
define("ComponentPropAssignment", MESSAGE, [("defID", "GUID"), ("value", "ComponentPropValue")])
define("SymbolData", MESSAGE, [("symbolID", "GUID"), ("symbolOverrides", "NodeChange", True)])
define("NodeChange", MESSAGE, [
    ("guid", "GUID"), ("parentIndex", "ParentIndex"), ("type", "NodeType"), ("name", STRING), ("visible", BOOL),
    ("opacity", FLOAT), ("size", "Vector"), ("transform", "Matrix"), ("fillPaints", "Paint", True),
    ("strokePaints", "Paint", True), ("strokeWeight", FLOAT), ("cornerRadius", FLOAT), ("stackMode", "StackMode"),
    ("stackSpacing", FLOAT), ("stackHorizontalPadding", FLOAT), ("stackVerticalPadding", FLOAT),
    ("stackPrimarySizing", "StackSize"), ("stackCounterSizing", "StackSize"), ("textData", "TextData"),
    ("fontName", "FontName"), ("fontSize", FLOAT), ("symbolData", "SymbolData"),
    ("componentPropDefs", "ComponentPropDef", True), ("componentPropRefs", "ComponentPropRef", True),
    ("componentPropAssignments", "ComponentPropAssignment", True), ("vectorData", "VectorData"),
    ("effects", "Effect", True), ("guidPath", "GUIDPath"), ("styleID", UINT), ("isStateGroup", BOOL),
    ("frameMaskDisabled", BOOL), ("overrideKey", "GUID"), ("styleType", "StyleType"),
    ("inheritFillStyleID", "GUID"), ("stackChildPrimaryGrow", FLOAT), ("textAutoResize", "TextAutoResize"),
    ("internalOnly", BOOL), ("backgroundColor", "Color"), ("resizeToFit", BOOL), ("arcData", "ArcData"),
])
define("ArcData", STRUCT, [("startingAngle", FLOAT), ("endingAngle", FLOAT), ("innerRadius", FLOAT)])
define("Blob", MESSAGE, [("bytes", BYTE, True)])
define("MessageType", ENUM, ["JOIN_START", "NODE_CHANGES"])
define("Message", MESSAGE, [("type", "MessageType"), ("nodeChanges", "NodeChange", True), ("blobs", "Blob", True)])


def fields_of(name):
    return next(d for d in DEFS if d[0] == name)


# ---------------------------------------------------------------- encoder
class W:
    def __init__(self):
        self.b = bytearray()

    def byte(self, v):
        self.b.append(v & 255)

    def uint(self, v):
        while True:
            c = v & 127
            v >>= 7
            if v:
                self.b.append(c | 128)
            else:
                self.b.append(c)
                return

    def int(self, v):
        self.uint((v << 1) ^ (v >> 31) if v >= 0 else ((-v - 1) << 1) | 1)

    def float(self, f):
        bits = struct.unpack("<I", struct.pack("<f", f))[0]
        bits = ((bits >> 23) | (bits << 9)) & 0xFFFFFFFF
        if bits & 255 == 0:
            self.byte(0)
            return
        self.b += struct.pack("<I", bits)

    def string(self, s):
        self.b += s.encode() + b"\0"


def encode_schema():
    w = W()
    w.uint(len(DEFS))
    for name, kind, fields in DEFS:
        w.string(name)
        w.byte(kind)
        w.uint(len(fields))
        for i, f in enumerate(fields):
            if kind == ENUM:
                w.string(f)
                w.int(UINT)
                w.byte(0)
                w.uint(i)
                continue
            fname, ftype = f[0], f[1]
            array = len(f) > 2 and f[2]
            w.string(fname)
            w.int(ftype if isinstance(ftype, int) else T(ftype))
            w.byte(1 if array else 0)
            w.uint(i + 1)
    return bytes(w.b)


def encode_value(w, ftype, value, array=False):
    if array:
        if ftype == BYTE:
            w.uint(len(value))
            w.b += bytes(value)
            return
        w.uint(len(value))
        for v in value:
            encode_value(w, ftype, v)
        return
    if isinstance(ftype, int):
        {BOOL: lambda v: w.byte(1 if v else 0), BYTE: w.byte, INT: w.int, UINT: w.uint, FLOAT: w.float,
         STRING: w.string}[ftype](value)
        return
    name, kind, fields = fields_of(ftype)
    if kind == ENUM:
        w.uint(fields.index(value))
    elif kind == STRUCT:
        for f in fields:
            encode_value(w, f[1], value[f[0]], len(f) > 2 and f[2])
    else:
        for i, f in enumerate(fields):
            if f[0] in value:
                w.uint(i + 1)
                encode_value(w, f[1], value[f[0]], len(f) > 2 and f[2])
        w.uint(0)


# ---------------------------------------------------------------- content
def guid(s, l):
    return {"sessionID": s, "localID": l}


def color(r, g, b, a=1.0):
    return {"r": r, "g": g, "b": b, "a": a}


def at(x, y):
    return {"m00": 1.0, "m01": 0.0, "m02": float(x), "m10": 0.0, "m11": 1.0, "m12": float(y)}


def solid(c):
    return {"type": "SOLID", "color": c, "opacity": 1.0, "visible": True}


def node(g, parent, pos, type_, name, **kw):
    n = {"guid": g, "type": type_, "name": name, "visible": True, "opacity": 1.0}
    if parent:
        n["parentIndex"] = {"guid": parent, "position": pos}
    n.update(kw)
    return n


def png_1x1(r, g, b):
    def chunk(t, data):
        c = struct.pack(">I", len(data)) + t + data
        return c + struct.pack(">I", zlib.crc32(t + data) & 0xFFFFFFFF)
    raw = b"\0" + bytes([r, g, b])
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


IMAGE_HASH = bytes(range(20))
DOC, PAGE, INTERNAL = guid(0, 0), guid(0, 1), guid(0, 2)

# Triangle network in a 50x50 normalized box: three vertices, three segments, one region.
net = struct.pack("<III", 3, 3, 1)
for x, y in ((0, 0), (50, 0), (25, 50)):
    net += struct.pack("<Iff", 0, x, y)
for a, b in ((0, 1), (1, 2), (2, 0)):
    net += struct.pack("<IIffIff", 0, a, 0, 0, b, 0, 0)
net += struct.pack("<II", 1, 1) + struct.pack("<IIII", 3, 0, 1, 2)

nodes = [
    node(DOC, None, "", "DOCUMENT", "Document"),
    node(PAGE, DOC, "!", "CANVAS", "Page 1", backgroundColor=color(0.9, 0.9, 0.9)),
    node(INTERNAL, DOC, "#", "CANVAS", "Internal Only Canvas", internalOnly=True),
    node(guid(0, 3), INTERNAL, "!", "RECTANGLE", "Brand/Blue", styleType="FILL", fillPaints=[solid(color(0, 0, 1))],
         size={"x": 10.0, "y": 10.0}, transform=at(0, 0)),
    # Auto layout card with a text whose second word is red.
    node(guid(1, 1), PAGE, "!", "FRAME", "Card", size={"x": 200.0, "y": 100.0}, transform=at(10, 20),
         fillPaints=[solid(color(1, 1, 1))], cornerRadius=8.0, stackMode="VERTICAL", stackSpacing=8.0,
         stackHorizontalPadding=16.0, stackVerticalPadding=16.0, stackPrimarySizing="RESIZE_TO_FIT",
         stackCounterSizing="FIXED",
         effects=[{"type": "DROP_SHADOW", "color": color(0, 0, 0, 0.25), "offset": {"x": 0.0, "y": 4.0},
                   "radius": 8.0, "spread": 0.0, "visible": True}]),
    node(guid(1, 2), guid(1, 1), "!", "TEXT", "Title", size={"x": 120.0, "y": 24.0}, transform=at(16, 16),
         fillPaints=[solid(color(0, 0, 0))], fontName={"family": "Inter", "style": "Bold", "postscript": ""},
         fontSize=20.0, textAutoResize="WIDTH_AND_HEIGHT",
         textData={"characters": "Hello Figma", "characterStyleIDs": [0] * 6 + [1] * 5,
                   "styleOverrideTable": [{"styleID": 1, "fillPaints": [solid(color(1, 0, 0))]}]}),
    node(guid(1, 3), PAGE, "#", "RECTANGLE", "Styled", size={"x": 50.0, "y": 50.0}, transform=at(300, 0),
         fillPaints=[solid(color(0, 0, 1))], inheritFillStyleID=guid(0, 3)),
    # Component with a text property, and an instance that sets it and overrides a fill.
    node(guid(2, 1), PAGE, "$", "SYMBOL", "Button", size={"x": 120.0, "y": 40.0}, transform=at(0, 200),
         fillPaints=[solid(color(0, 1, 0))],
         componentPropDefs=[{"id": guid(3, 1), "name": "Label", "type": "TEXT",
                             "initialValue": {"textValue": {"characters": "OK"}}}]),
    node(guid(2, 2), guid(2, 1), "!", "TEXT", "Label", size={"x": 30.0, "y": 20.0}, transform=at(10, 10),
         fillPaints=[solid(color(0, 0, 0))], fontName={"family": "Inter", "style": "Regular", "postscript": ""},
         fontSize=14.0, textData={"characters": "OK"},
         componentPropRefs=[{"defID": guid(3, 1), "componentPropNodeField": "TEXT_DATA"}]),
    node(guid(2, 4), guid(2, 1), "#", "RECTANGLE", "Dot", size={"x": 8.0, "y": 8.0}, transform=at(100, 16),
         fillPaints=[solid(color(0, 0, 0))]),
    node(guid(2, 3), PAGE, "%", "INSTANCE", "Button", size={"x": 160.0, "y": 40.0}, transform=at(200, 200),
         symbolData={"symbolID": guid(2, 1),
                     "symbolOverrides": [{"guidPath": {"guids": [guid(2, 4)]},
                                          "fillPaints": [solid(color(1, 0, 0))]}]},
         componentPropAssignments=[{"defID": guid(3, 1), "value": {"textValue": {"characters": "Go"}}}]),
    # A vector network, a group and an image fill.
    node(guid(4, 1), PAGE, "&", "VECTOR", "Triangle", size={"x": 100.0, "y": 100.0}, transform=at(0, 300),
         fillPaints=[solid(color(1, 0.5, 0))], vectorData={"vectorNetworkBlob": 0,
                                                           "normalizedSize": {"x": 50.0, "y": 50.0}}),
    node(guid(4, 3), PAGE, "'", "FRAME", "Group", size={"x": 20.0, "y": 20.0}, transform=at(400, 0),
         resizeToFit=True),
    node(guid(4, 5), guid(4, 3), "!", "ELLIPSE", "Ring", size={"x": 20.0, "y": 20.0}, transform=at(0, 0),
         fillPaints=[solid(color(0, 0, 0))], arcData={"startingAngle": 0.0, "endingAngle": 3.1415927,
                                                       "innerRadius": 0.5}),
    node(guid(4, 4), PAGE, "(", "RECTANGLE", "Photo", size={"x": 40.0, "y": 40.0}, transform=at(500, 0),
         fillPaints=[{"type": "IMAGE", "opacity": 1.0, "visible": True, "image": {"hash": IMAGE_HASH, "name": "p"},
                      "imageScaleMode": "FILL"}]),
    node(guid(4, 6), PAGE, ")", "STICKY", "Note", size={"x": 10.0, "y": 10.0}, transform=at(0, 0)),
]

message = {"type": "NODE_CHANGES", "nodeChanges": nodes, "blobs": [{"bytes": net}]}


def main():
    schema = encode_schema()
    w = W()
    encode_value(w, "Message", message)
    deflate = zlib.compressobj(9, zlib.DEFLATED, -15)
    schema_c = deflate.compress(schema) + deflate.flush()
    message_c = zstandard.ZstdCompressor().compress(bytes(w.b))
    canvas = b"fig-kiwi" + struct.pack("<I", 48)
    canvas += struct.pack("<I", len(schema_c)) + schema_c + struct.pack("<I", len(message_c)) + message_c
    out = os.path.join(os.path.dirname(__file__), "..", "tests", "data", "sample.fig")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("canvas.fig", canvas, compress_type=zipfile.ZIP_STORED)
        z.writestr("images/" + IMAGE_HASH.hex(), png_1x1(0, 255, 0))
        z.writestr("meta.json", '{"file_name": "sample"}')
    with open(out, "wb") as f:
        f.write(buf.getvalue())
    print("wrote", os.path.normpath(out), len(buf.getvalue()), "bytes")


if __name__ == "__main__":
    main()
