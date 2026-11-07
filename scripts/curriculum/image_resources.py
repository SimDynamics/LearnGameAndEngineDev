"""Local curricular-image authoring checks; never render, fetch or modify an asset.

Header/container checks do not replace the native image decoder. In particular,
they certify neither the legibility nor the provenance of a screenshot.
"""
from pathlib import Path, PurePosixPath
import os
import re
import stat
import struct
import zlib

FIELDS = frozenset({"type", "id", "imageId", "src", "alt", "caption"})
MAX_BYTES = 64 * 1024 * 1024
MAX_PIXELS = 40_000_000
MAX_DIMENSION = 32768
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
JPEG_SOF = frozenset({0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7,
                      0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF})


def _require(condition, message):
    if not condition:
        raise ValueError(message)


def _text(value, field):
    _require(isinstance(value, str) and bool(value.strip()) and "\0" not in value,
             f"image.{field} requiere texto no vacío sin NUL")
    try:
        value.encode("utf-8", errors="strict")
    except UnicodeError as error:
        raise ValueError(f"image.{field} contiene Unicode no válido") from error


def validate_contract(block):
    """Validate the same immutable presentation payload accepted by the loader."""
    _require(type(block) is dict and block.get("type") == "image", "Se esperaba un bloque image")
    _require(set(block) <= FIELDS, "image contiene campos no admitidos")
    if "id" in block:
        _text(block["id"], "id")
    identifier = block.get("imageId")
    _require(isinstance(identifier, str) and re.fullmatch(r"[a-z0-9_]{1,128}", identifier) is not None,
             "imageId requiere 1–128 letras minúsculas ASCII, dígitos o guiones bajos")
    _text(block.get("alt"), "alt")
    if "caption" in block:
        _text(block["caption"], "caption")
    source = block.get("src")
    _text(source, "src")
    _require(len(source.encode("utf-8")) <= 1024, "image.src supera 1024 bytes UTF-8")
    _require(source.startswith("images/") and not any(ord(c) < 32 or ord(c) == 127 or c in '\\:<>"|?*#' for c in source),
             "image.src debe ser una ruta local images/ con barras /, sin URI ni caracteres reservados")
    parts = source.split("/")
    for part in parts:
        _require(part not in ("", ".", "..") and part[-1] not in ". ", "image.src contiene un segmento no válido")
        stem = part.split(".", 1)[0].lower()
        reserved = stem in {"con", "prn", "aux", "nul", "clock$", "conin$", "conout$"} or re.fullmatch(r"(?:com|lpt)[1-9¹²³]", stem)
        _require(not reserved, "image.src contiene un nombre de dispositivo Windows")
    path = PurePosixPath(source)
    _require(path.suffix.lower() in {".png", ".jpg", ".jpeg"} and path.stem == identifier,
             "image.src requiere PNG/JPG/JPEG y el nombre del archivo debe coincidir con imageId")
    return path


def _dimensions(width, height):
    _require(0 < width <= MAX_DIMENSION and 0 < height <= MAX_DIMENSION and width * height <= MAX_PIXELS,
             "La imagen supera 32768 por dimensión o 40 millones de píxeles, o tiene dimensiones vacías")
    return width, height


def _png(data):
    _require(data.startswith(PNG_SIGNATURE), "La extensión PNG no coincide con su firma")
    at, dimensions, seen_data, seen_end = 8, None, False, False
    while at < len(data):
        _require(at + 12 <= len(data), "Contenedor PNG truncado")
        size, = struct.unpack_from(">I", data, at)
        end = at + 12 + size
        _require(end <= len(data), "Chunk PNG fuera del archivo")
        kind, payload = data[at + 4:at + 8], data[at + 8:at + 8 + size]
        checksum, = struct.unpack_from(">I", data, at + 8 + size)
        _require(zlib.crc32(kind + payload) & 0xffffffff == checksum, "CRC PNG no válido")
        if dimensions is None:
            _require(kind == b"IHDR" and size == 13, "La imagen PNG no empieza por IHDR")
            width, height = struct.unpack_from(">II", payload)
            dimensions = _dimensions(width, height)
        elif kind == b"IHDR":
            raise ValueError("Cabecera IHDR PNG repetida")
        if kind == b"IDAT":
            seen_data = True
        if kind == b"IEND":
            _require(size == 0 and seen_data, "PNG sin datos de imagen o IEND no válido")
            seen_end = True
            break
        at = end
    _require(dimensions is not None and seen_end, "PNG sin cierre IEND")
    return dimensions


def _jpeg(data):
    _require(data.startswith(b"\xff\xd8"), "La extensión JPEG no coincide con su firma")
    at, dimensions = 2, None
    while at < len(data):
        _require(data[at] == 0xff, "Marcador JPEG no válido")
        while at < len(data) and data[at] == 0xff:
            at += 1
        _require(at < len(data), "Marcador JPEG truncado")
        marker, at = data[at], at + 1
        _require(marker not in {0, 0xD8, 0xD9} and not 0xD0 <= marker <= 0xD7, "Segmento JPEG fuera de orden")
        _require(at + 2 <= len(data), "Longitud JPEG ausente")
        size, = struct.unpack_from(">H", data, at)
        _require(size >= 2 and at + size <= len(data), "Segmento JPEG truncado")
        if marker in JPEG_SOF:
            _require(size >= 8 and dimensions is None, "Cabecera de dimensiones JPEG no válida")
            height, width = struct.unpack_from(">HH", data, at + 3)
            dimensions = _dimensions(width, height)
        if marker == 0xDA:
            _require(dimensions is not None and data.find(b"\xff\xd9", at + size) >= 0,
                     "JPEG sin dimensiones o cierre EOI")
            return dimensions
        at += size
    raise ValueError("JPEG sin datos SOS")


def validate_resource(block, content_root):
    """Check a local regular resource without following it outside content_root.

    Return its actual (width, height). Resource absence is an authoring error;
    runtime handles disappearance/decoding errors explicitly in the lesson.
    """
    relative = validate_contract(block)
    try:
        root = Path(content_root).resolve(strict=True)
        _require(root.is_dir(), "La raíz content de imágenes no es una carpeta")
        logical = root.joinpath(*relative.parts)
        actual = logical.resolve(strict=True)
        _require(actual.is_relative_to(root) and actual != root, "La imagen redirige fuera de content")
        _require(stat.S_ISREG(actual.stat().st_mode), "El recurso de imagen no es un archivo regular")
        with actual.open("rb") as stream:
            opened = os.fstat(stream.fileno())
            _require(stat.S_ISREG(opened.st_mode), "El recurso de imagen no es un archivo regular")
            _require(0 < opened.st_size <= MAX_BYTES, "La imagen está vacía o supera 64 MiB")
            current = logical.stat()
            _require(logical.resolve(strict=True) == actual and os.path.samestat(opened, current),
                     "El recurso de imagen cambió durante la comprobación")
            data = stream.read(MAX_BYTES + 1)
            _require(len(data) == opened.st_size and os.fstat(stream.fileno()).st_size == opened.st_size,
                     "El recurso de imagen cambió de tamaño durante la comprobación")
    except OSError as error:
        raise ValueError("Recurso de imagen local ausente o no legible") from error
    return _png(data) if relative.suffix.lower() == ".png" else _jpeg(data)
