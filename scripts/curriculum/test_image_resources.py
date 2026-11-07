"""Synthetic local image contracts only; no screenshots, UI or learner profiles."""
from copy import deepcopy
from importlib import util
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

SCRIPTS = Path(__file__).parent
spec = util.spec_from_file_location("image_state_layout_test", SCRIPTS / "stable_state_layout.py")
layout = util.module_from_spec(spec)
spec.loader.exec_module(layout)
images = layout.image_resources
validator_spec = util.spec_from_file_location("image_curricular_validator_test", SCRIPTS / "validar_json_programacion.py")
validator = util.module_from_spec(validator_spec)
validator_spec.loader.exec_module(validator)


def block(**changes):
    value = {"type": "image", "imageId": "qa_setup_01", "src": "images/qa/qa_setup_01.png",
             "alt": "Diagrama sintético de prueba.", "caption": "Recurso QA, no una captura de una acción real."}
    return dict(value, **changes)


def png(width=1, height=1):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    # A real tiny grayscale PNG. Oversized-header negatives deliberately need
    # no huge pixel allocation: their dimensions fail before image data is used.
    return (images.PNG_SIGNATURE + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(b"\0\x80")) + chunk(b"IEND", b""))


def jpeg():
    def segment(marker, data):
        return b"\xff" + bytes([marker]) + struct.pack(">H", len(data) + 2) + data
    # One grayscale pixel: all transform coefficients are zero. A one-symbol
    # DC/AC Huffman table encodes DC=0 and EOB as 00, padded with six 1 bits.
    table = bytes([1] + [0] * 15 + [0])
    return (b"\xff\xd8" + segment(0xDB, b"\0" + bytes([1] * 64))
            + segment(0xC0, b"\x08\0\x01\0\x01\x01\x01\x11\0")
            + segment(0xC4, b"\0" + table + b"\x10" + table)
            + segment(0xDA, b"\x01\x01\0\0\x3f\0") + b"\x3f\xff\xd9")


class ImageResourceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="lgaed-image-contract-")
        self.root = Path(self.temporary.name)
        self.path = self.root / "images/qa/qa_setup_01.png"
        self.path.parent.mkdir(parents=True)
        self.path.write_bytes(png())

    def tearDown(self):
        self.temporary.cleanup()

    def test_png_jpeg_and_optional_caption_are_checked_without_writes(self):
        before = self.path.read_bytes()
        value = block()
        self.assertEqual(images.validate_resource(value, self.root), (1, 1))
        self.assertEqual(self.path.read_bytes(), before)
        self.assertEqual(value, block())
        del value["caption"]
        images.validate_contract(value)
        images.validate_contract(dict(value, id="qa_image_block"))
        for extension in ("jpg", "jpeg", "JPEG"):
            source = f"images/qa/qa_setup_01.{extension}"
            (self.root / source).write_bytes(jpeg())
            self.assertEqual(images.validate_resource(block(src=source), self.root), (1, 1))

    def test_required_fields_types_and_closed_schema(self):
        for field in ("imageId", "src", "alt"):
            value = block()
            del value[field]
            with self.subTest(field=field), self.assertRaises(ValueError):
                images.validate_contract(value)
        for change in ({"imageId": "A"}, {"imageId": "a-b"}, {"imageId": "a" * 129}, {"imageId": False},
                       {"alt": " \t\n\u00a0"}, {"alt": None}, {"alt": "x\0y"}, {"alt": "\ud800"},
                       {"caption": ""}, {"caption": []}, {"caption": "\u00a0"}, {"width": 100},
                       {"height": 100}, {"id": ""}, {"id": "\u00a0"}, {"id": False},
                       {"id": "x\0y"}, {"id": "\ud800"}, {"url": "https://example.test/a.png"}):
            with self.subTest(change=repr(change)), self.assertRaises(ValueError):
                images.validate_contract(block(**change))

    def test_paths_cannot_escape_or_use_windows_aliases(self):
        invalid = ("/images/qa_setup_01.png", "C:/images/qa_setup_01.png", "images\\qa_setup_01.png",
                   "https://example.test/qa_setup_01.png", "images/../qa_setup_01.png", "images/./qa_setup_01.png",
                   "images//qa_setup_01.png", "images/qa_setup_01.png:stream", "images/qa_setup_01.png?x=1",
                   "images/qa_setup_01.png#part", "images/a./qa_setup_01.png", "images/a /qa_setup_01.png",
                   "images/con/qa_setup_01.png", "images/AUX.bin/qa_setup_01.png", "images/clock$/qa_setup_01.png",
                   "images/conin$/qa_setup_01.png", "images/COM¹/qa_setup_01.png", "images/LPT9/qa_setup_01.png",
                   "images/x\n/qa_setup_01.png", "images/x\x7f/qa_setup_01.png", "images/another.png", "images/qa_setup_01.svg")
        for source in invalid:
            with self.subTest(source=repr(source)), self.assertRaises(ValueError):
                images.validate_contract(block(src=source))
        with self.assertRaises(ValueError):
            images.validate_contract(block(src="images/" + "á" * 512 + "/qa_setup_01.png"))

    def test_missing_directory_wrong_signature_and_truncated_data(self):
        for data in (b"", b"not an image", png()[:-1], png()[:30], jpeg()):
            self.path.write_bytes(data)
            with self.subTest(data=data[:12]), self.assertRaises(ValueError):
                images.validate_resource(block(), self.root)
        damaged = bytearray(png())
        damaged[20] ^= 1
        self.path.write_bytes(damaged)
        with self.assertRaises(ValueError):
            images.validate_resource(block(), self.root)
        self.path.unlink()
        with self.assertRaises(ValueError):
            images.validate_resource(block(), self.root)
        self.path.mkdir()
        with self.assertRaises(ValueError):
            images.validate_resource(block(), self.root)

    def test_real_file_budget_and_dimension_boundaries(self):
        with self.path.open("wb") as stream:
            stream.write(png())
            stream.truncate(images.MAX_BYTES + 1)
        with self.assertRaises(ValueError):
            images.validate_resource(block(), self.root)
        self.assertEqual(images._dimensions(32768, 1), (32768, 1))
        self.assertEqual(images._dimensions(8000, 5000), (8000, 5000))
        for dimensions in ((0, 1), (1, 0), (32769, 1), (1, 32769), (8000, 5001)):
            self.path.write_bytes(png(*dimensions))
            with self.subTest(dimensions=dimensions), self.assertRaises(ValueError):
                images.validate_resource(block(), self.root)

    def test_canonical_external_link_is_rejected(self):
        outside = self.root / "outside"
        outside.mkdir()
        (outside / self.path.name).write_bytes(png())
        content = self.root / "content"
        (content / "images").mkdir(parents=True)
        try:
            (content / "images/qa").symlink_to(outside, target_is_directory=True)
        except OSError as error:
            self.skipTest(f"Creating an isolated symlink is not available: {error}")
        with self.assertRaises(ValueError):
            images.validate_resource(block(), content)

    def test_state_identity_preserves_source_but_not_editorial_descriptions(self):
        original = block()
        updated = block(alt="Otra descripción pública.", caption="Otro pie público.")
        self.assertEqual(layout.state_fingerprint(original), layout.state_fingerprint(updated))
        self.assertNotEqual(layout.fingerprint(original), layout.fingerprint(updated))
        self.assertNotEqual(layout.state_fingerprint(original), layout.state_fingerprint(block(src="images/other/qa_setup_01.png")))
        self.assertNotEqual(layout.state_fingerprint(original), layout.state_fingerprint(block(imageId="qa_other", src="images/qa_other.png")))
        self.assertIn("image", layout.RETIRABLE_TYPES)
        self.assertNotIn("image", validator.EXERCISES)
        self.assertEqual(validator.FIELDS["image"], images.FIELDS)

    def test_layout_and_reference_are_valid_without_fabricated_activity(self):
        source = {"id": "qa_curricular_image", "title": "QA", "pages": [{"blocks": [
            {"type": "cpp_compiler", "initialCode": "int main(){}", "outputCheck": {"cases": [
                {"standardInput": "", "outputs": ["ok"]}]}}, block()]}]}
        mapped = layout.prepare_v2(source, "es", {"es": source})
        current = mapped["stateLayout"]["current"][0]["blocks"]
        mapped["referenceAfter"] = [{"activity": current[0], "blocks": [current[1]]}]
        layout.validate(mapped)
        self.assertEqual(mapped["pages"], source["pages"])
        invalid = deepcopy(mapped)
        invalid["pages"][0]["blocks"][1]["width"] = 400
        with self.assertRaises(ValueError):
            layout.validate(invalid)

    def test_curricular_validator_reports_resource_errors_at_the_owner(self):
        before = list(validator.ERRORS)
        try:
            validator.ERRORS.clear()
            validator.validate_image_contract(block(), "image-owner", self.root)
            self.assertEqual(validator.ERRORS, [])
            self.path.unlink()
            validator.validate_image_contract(block(), "image-owner", self.root)
            self.assertEqual(len(validator.ERRORS), 1)
            self.assertIn("image-owner", validator.ERRORS[0])
        finally:
            validator.ERRORS[:] = before


if __name__ == "__main__":
    unittest.main()
