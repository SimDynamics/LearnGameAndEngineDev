"""Build a host-signed Clang 21 split and a byte-exact sparse installation recipe.

The Android SDK tools compile the manifest, align the APK and sign it with the
same host keystore as the base APK. No signing key enters generated assets. The
envelope retains every APK byte other than the twelve unchanged ELF payloads;
Android reconstructs it from the two official HTTPS FoldCode archives before
submitting it to PackageInstaller. This script does not install or publish APKs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path
from xml.sax.saxutils import quoteattr


RELEASE_URL = "https://github.com/FifiLeih/FoldCode/releases/download/v1.0.0/"
NESTED_ENTRY = "payload/cpp-runtime-arm64.zip"
ENVELOPE_ASSET = "runtime/clang21_installation/envelope.bin"
SPLIT_NAME = "clang21"
CHUNK = 1024 * 1024
MAX_BYTES = 1024 * 1024 * 1024
LIBRARIES = (
    "libc++_shared.so", "libfoldclang.so", "libLLVM.so", "libclang-cpp.so",
    "libffi.so", "libfoldlldcore.so", "libiconv.so", "libicudataxxx.so",
    "libicuucxxx.so", "libxml2xxx.so", "libzstdxx.so", "libzzz.so",
)
BASE_LIBRARIES = frozenset(LIBRARIES[:2])


@dataclass(frozen=True)
class Archive:
    name: str
    bytes: int
    sha256: str

    def metadata(self):
        return {"name": self.name, "url": RELEASE_URL + self.name,
                "bytes": self.bytes, "sha256": self.sha256}


ARCHIVES = (
    Archive("foldcode-base-native-arm64-v1.zip", 16_443_707,
            "5215289fdab11c43b8d762c788357ee3eb98d1b91e0c46ae53c80528f04970f1"),
    Archive("foldcode-cpp-1.0.0-3.fcex", 95_833_816,
            "82677ce2d912127896a75a113d522455c773f905849e0e56d49c9b71c2930407"),
)


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def valid(path: Path, size: int, digest: str):
    return path.is_file() and path.stat().st_size == size and sha256(path) == digest


def verify_elf(header: bytes, name: str):
    require(len(header) >= 20 and header[:6] == b"\x7fELF\x02\x01" and
            header[16:20] == b"\x03\x00\xb7\x00", f"Not an ELF64 AArch64 shared object: {name}")


def archive_entry(archive: zipfile.ZipFile, name: str):
    matches = [info for info in archive.infolist() if info.filename == name]
    require(len(matches) == 1, f"Missing or duplicate archive entry: {name}")
    info = matches[0]
    require(not info.is_dir() and 0 < info.file_size <= MAX_BYTES and not info.flag_bits & 1,
            f"Invalid archive entry: {name}")
    return info


def stream_digest(stream, size: int):
    digest = hashlib.sha256()
    header = b""
    remaining = size
    while remaining:
        data = stream.read(min(CHUNK, remaining))
        require(bool(data), "Truncated resource")
        if len(header) < 20:
            header += data[:20 - len(header)]
        digest.update(data)
        remaining -= len(data)
    require(stream.read(1) == b"", "Oversized resource")
    return digest.hexdigest(), header


def verify_sources(native: Path, cache: Path, work: Path, archives=ARCHIVES):
    require(native.is_dir(), "The prepared native library directory does not exist")
    require({path.name for path in native.iterdir()} == set(LIBRARIES),
            "The prepared native directory must contain exactly the twelve Clang 21 libraries")
    require(len(archives) == 2, "Two official FoldCode archives are required")
    for archive in archives:
        require(valid(cache / archive.name, archive.bytes, archive.sha256),
                f"Official archive SHA-256/size mismatch: {archive.name}")
    work.mkdir(parents=True, exist_ok=True)
    nested = work / "verified-cpp-runtime.zip"
    metadata = []
    try:
        with zipfile.ZipFile(cache / archives[1].name) as extension:
            info = archive_entry(extension, NESTED_ENTRY)
            with extension.open(info) as source, nested.open("wb") as target:
                shutil.copyfileobj(source, target, CHUNK)
            require(nested.stat().st_size == info.file_size, "Incomplete nested runtime archive")
        with zipfile.ZipFile(cache / archives[0].name) as base, zipfile.ZipFile(nested) as runtime:
            for name in LIBRARIES:
                source_archive = base if name in BASE_LIBRARIES else runtime
                info = archive_entry(source_archive, "native/" + name)
                with source_archive.open(info) as source:
                    original_digest, header = stream_digest(source, info.file_size)
                verify_elf(header, name)
                prepared = native / name
                require(valid(prepared, info.file_size, original_digest),
                        f"Prepared library differs from its official original: {name}")
                with prepared.open("rb") as source:
                    verify_elf(source.read(20), name)
                metadata.append({"name": name, "bytes": info.file_size, "sha256": original_digest,
                                 "archive": archives[0 if name in BASE_LIBRARIES else 1].name,
                                 "entry": "native/" + name,
                                 "nestedEntry": None if name in BASE_LIBRARIES else NESTED_ENTRY})
    finally:
        nested.unlink(missing_ok=True)
    return metadata


def manifest(package: str, version: int, minimum: int, target: int):
    require(re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*(\.[A-Za-z][A-Za-z0-9_]*)+", package), "Invalid package name")
    require(0 < version <= 2_100_000_000, "Invalid version code")
    require(24 <= minimum <= target, "Clang split requires Android API 24 or newer")
    return f'''<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package={quoteattr(package)} split="clang21" android:isFeatureSplit="true"
    android:versionCode="{version}">
    <uses-sdk android:minSdkVersion="{minimum}" android:targetSdkVersion="{target}" />
    <application android:hasCode="false" android:extractNativeLibs="true" />
</manifest>
'''


def run(command):
    result = subprocess.run([str(item) for item in command], text=True,
                            encoding="utf-8", errors="replace", capture_output=True)
    require(result.returncode == 0, f"Android tool failed ({Path(str(command[0])).name}):\n{result.stdout}\n{result.stderr}")
    return result.stdout + result.stderr


def signer_command(java: Path, signer: Path):
    if signer.suffix.lower() == ".jar":
        return [java, "-jar", signer]
    if signer.suffix.lower() in {".bat", ".cmd"}:
        jar = signer.parent / "lib" / "apksigner.jar"
        require(jar.is_file(), "Pass the Android SDK lib/apksigner.jar for this platform")
        return [java, "-jar", jar]
    return [signer]


def certificate_from_verification(output: str):
    matches = re.findall(r"^Signer #\d+ certificate SHA-256 digest: ([0-9a-fA-F]{64})\s*$", output, re.MULTILINE)
    require(len(matches) == 1, "Expected exactly one APK signing certificate")
    return matches[0].lower()


def build_signed_apk(args, native: Path, work: Path):
    xml = work / "AndroidManifest.xml"
    xml.write_text(manifest(args.package_name, args.version_code, args.min_sdk, args.target_sdk), encoding="utf-8")
    stub, unsigned, aligned, signed = (work / name for name in
                                      ("manifest.apk", "unsigned.apk", "aligned.apk", "clang21.apk"))
    run([args.aapt2, "link", "-o", stub, "--manifest", xml, "-I", args.android_jar])
    with zipfile.ZipFile(stub) as source:
        binary_manifest = source.read("AndroidManifest.xml")
    require(binary_manifest[:4] == b"\x03\x00\x08\x00", "aapt2 did not produce a binary Android manifest")
    with zipfile.ZipFile(unsigned, "w", compression=zipfile.ZIP_STORED, allowZip64=True) as apk:
        info = zipfile.ZipInfo("AndroidManifest.xml", date_time=(2009, 1, 1, 0, 0, 0))
        apk.writestr(info, binary_manifest)
        for name in LIBRARIES:
            info = zipfile.ZipInfo("lib/arm64-v8a/" + name, date_time=(2009, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_STORED
            info.external_attr = 0o100644 << 16
            with (native / name).open("rb") as source, apk.open(info, "w") as target:
                shutil.copyfileobj(source, target, CHUNK)
    run([args.zipalign, "-P", "16", "-f", "4", unsigned, aligned])
    signer = signer_command(args.java, args.apksigner)
    require(args.store_password_env in os.environ and args.key_password_env in os.environ,
            "Set the explicitly named keystore password environment variables on the build host")
    run(signer + ["sign", "--ks", args.keystore, "--ks-key-alias", args.key_alias,
                  "--ks-pass", "env:" + args.store_password_env,
                  "--key-pass", "env:" + args.key_password_env,
                  "--v1-signing-enabled", "false", "--v2-signing-enabled", "true",
                  "--v3-signing-enabled", "true", "--v4-signing-enabled", "false",
                  "--out", signed, aligned])
    verification = run(signer + ["verify", "--verbose", "--print-certs", signed])
    certificate = certificate_from_verification(verification)
    if args.expected_certificate_sha256:
        expected = args.expected_certificate_sha256.replace(":", "").lower()
        require(certificate == expected, "The split signing certificate differs from the base APK")
    run([args.zipalign, "-c", "-P", "16", "4", signed])
    return signed, certificate, verification


def payload_range(source, info: zipfile.ZipInfo):
    require(info.compress_type == zipfile.ZIP_STORED and info.compress_size == info.file_size,
            f"Native payload must be ZIP_STORED: {info.filename}")
    require(not info.flag_bits & 1, "Encrypted native payload is not supported")
    source.seek(info.header_offset)
    header = source.read(30)
    require(len(header) == 30, "Truncated APK local ZIP header")
    signature, _, flags, method, _, _, _, _, _, name_size, extra_size = struct.unpack("<4s5H3I2H", header)
    require(signature == b"PK\x03\x04" and method == zipfile.ZIP_STORED and not flags & 1,
            "Invalid APK local ZIP header")
    name = source.read(name_size).decode("utf-8" if flags & 0x800 else "cp437")
    require(name == info.filename, "APK central/local filenames differ")
    return info.header_offset + 30 + name_size + extra_size, info.file_size


def copy_range(source, target, offset: int, size: int):
    source.seek(offset)
    remaining = size
    while remaining:
        data = source.read(min(CHUNK, remaining))
        require(bool(data), "Truncated APK segment")
        target.write(data)
        remaining -= len(data)


def create_recipe(apk: Path, envelope: Path, native: Path, libraries, package: str,
                  version: int, certificate: str, archives=ARCHIVES):
    require(len(libraries) == 12 and {item["name"] for item in libraries} == set(LIBRARIES),
            "The sparse recipe requires all twelve libraries")
    library_map = {item["name"]: item for item in libraries}
    ranges = []
    with apk.open("rb") as source, zipfile.ZipFile(apk) as zip_apk:
        paths = [info.filename for info in zip_apk.infolist()]
        require(len(paths) == len(set(paths)), "Duplicate APK ZIP entry")
        expected = {"lib/arm64-v8a/" + name for name in LIBRARIES}
        require({name for name in paths if name.startswith("lib/")} == expected,
                "APK must contain exactly the twelve ARM64 libraries")
        for name in LIBRARIES:
            info = zip_apk.getinfo("lib/arm64-v8a/" + name)
            offset, size = payload_range(source, info)
            require(size == library_map[name]["bytes"] and offset + size <= zip_apk.start_dir,
                    "Invalid APK native payload extent")
            source.seek(offset)
            digest = hashlib.sha256()
            remaining = size
            while remaining:
                data = source.read(min(CHUNK, remaining))
                require(bool(data), "Truncated APK native payload")
                digest.update(data)
                remaining -= len(data)
            require(digest.hexdigest() == library_map[name]["sha256"], f"APK library SHA-256 mismatch: {name}")
            require(valid(native / name, size, library_map[name]["sha256"]), f"Prepared library changed: {name}")
            ranges.append((offset, size, name))
    envelope.parent.mkdir(parents=True, exist_ok=True)
    segments = []
    cursor = envelope_offset = 0
    with apk.open("rb") as source, envelope.open("wb") as target:
        for offset, size, name in sorted(ranges):
            require(offset >= cursor, "Overlapping APK native payloads")
            if offset > cursor:
                count = offset - cursor
                copy_range(source, target, cursor, count)
                segments.append({"kind": "envelope", "offset": envelope_offset, "bytes": count})
                envelope_offset += count
            segments.append({"kind": "library", "name": name, "bytes": size})
            cursor = offset + size
        count = apk.stat().st_size - cursor
        require(count > 0, "APK is missing its signing block or central directory")
        copy_range(source, target, cursor, count)
        segments.append({"kind": "envelope", "offset": envelope_offset, "bytes": count})
    recipe = {
        "format": 1, "packageName": package, "versionCode": version,
        "certificateSha256": certificate, "splitName": SPLIT_NAME,
        "apk": {"bytes": apk.stat().st_size, "sha256": sha256(apk)},
        "envelope": {"file": ENVELOPE_ASSET, "bytes": envelope.stat().st_size, "sha256": sha256(envelope)},
        "archives": [archive.metadata() for archive in archives],
        "libraries": libraries, "segments": segments,
    }
    validate_recipe(recipe)
    return recipe


def validate_recipe(recipe):
    require(recipe["format"] == 1 and recipe["splitName"] == SPLIT_NAME, "Invalid recipe format/split")
    require(re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*(\.[A-Za-z][A-Za-z0-9_]*)+", recipe["packageName"]), "Invalid recipe package")
    require(0 < recipe["versionCode"] <= 2_100_000_000, "Invalid recipe version")
    require(re.fullmatch("[0-9a-f]{64}", recipe["certificateSha256"]), "Invalid recipe certificate")
    libraries = recipe["libraries"]
    require(len(libraries) == 12 and {item["name"] for item in libraries} == set(LIBRARIES), "Invalid recipe library set")
    require(len(recipe["archives"]) == 2 and {item["name"] for item in recipe["archives"]} == {item.name for item in ARCHIVES},
            "Invalid recipe archive set")
    archive_names = {item["name"] for item in recipe["archives"]}
    for archive in recipe["archives"]:
        require(archive["url"] == RELEASE_URL + archive["name"], "Nonofficial archive URL")
        require(0 < archive["bytes"] <= MAX_BYTES and re.fullmatch("[0-9a-f]{64}", archive["sha256"]), "Invalid archive integrity metadata")
    for library in libraries:
        require(library["archive"] in archive_names and library["entry"] == "native/" + library["name"] and
                library["nestedEntry"] == (None if library["name"] in BASE_LIBRARIES else NESTED_ENTRY), "Invalid library source")
        require(0 < library["bytes"] <= MAX_BYTES and re.fullmatch("[0-9a-f]{64}", library["sha256"]), "Invalid library integrity metadata")
    for item in (recipe["apk"], recipe["envelope"]):
        require(0 < item["bytes"] <= MAX_BYTES and re.fullmatch("[0-9a-f]{64}", item["sha256"]), "Invalid APK/envelope integrity metadata")
    require(recipe["envelope"]["file"] == ENVELOPE_ASSET and recipe["envelope"]["bytes"] <= 4 * CHUNK,
            "Envelope unexpectedly includes native payload")
    envelope_offset = total = 0
    seen = set()
    by_name = {item["name"]: item for item in libraries}
    for segment in recipe["segments"]:
        require(0 < segment["bytes"] <= MAX_BYTES, "Invalid segment size")
        total += segment["bytes"]
        if segment["kind"] == "envelope":
            require(segment["offset"] == envelope_offset, "Envelope segments overlap or have gaps")
            envelope_offset += segment["bytes"]
        else:
            require(segment["kind"] == "library" and segment["name"] in by_name and segment["name"] not in seen,
                    "Unknown or duplicate library segment")
            require(segment["bytes"] == by_name[segment["name"]]["bytes"], "Library segment size differs")
            seen.add(segment["name"])
    require(seen == set(LIBRARIES) and total == recipe["apk"]["bytes"] and
            envelope_offset == recipe["envelope"]["bytes"], "Incomplete APK recipe")


def reconstruct(recipe, envelope: Path, native: Path, output: Path):
    validate_recipe(recipe)
    require(valid(envelope, recipe["envelope"]["bytes"], recipe["envelope"]["sha256"]), "Envelope SHA-256 mismatch")
    for library in recipe["libraries"]:
        require(valid(native / library["name"], library["bytes"], library["sha256"]), "Library SHA-256 mismatch")
    temporary = output.with_name(output.name + ".part")
    output.parent.mkdir(parents=True, exist_ok=True)
    try:
        with envelope.open("rb") as source, temporary.open("wb") as target:
            for segment in recipe["segments"]:
                if segment["kind"] == "envelope":
                    copy_range(source, target, segment["offset"], segment["bytes"])
                else:
                    with (native / segment["name"]).open("rb") as library:
                        copy_range(library, target, 0, segment["bytes"])
        require(valid(temporary, recipe["apk"]["bytes"], recipe["apk"]["sha256"]), "Reconstructed APK SHA-256 mismatch")
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("native-dir", "archive-cache", "android-jar", "aapt2", "zipalign", "apksigner", "java", "keystore", "output-assets", "work-dir"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--package-name", required=True)
    parser.add_argument("--version-code", type=int, required=True)
    parser.add_argument("--min-sdk", type=int, default=24)
    parser.add_argument("--target-sdk", type=int, required=True)
    parser.add_argument("--key-alias", default="androiddebugkey")
    parser.add_argument("--store-password-env", default="LGP_CLANG21_STORE_PASSWORD")
    parser.add_argument("--key-password-env", default="LGP_CLANG21_KEY_PASSWORD")
    parser.add_argument("--expected-certificate-sha256")
    args = parser.parse_args()
    try:
        # Complete source validation precedes APK generation and output modification.
        libraries = verify_sources(args.native_dir, args.archive_cache, args.work_dir)
        signed, certificate, verification = build_signed_apk(args, args.native_dir, args.work_dir)
        destination = args.output_assets / "runtime" / "clang21_installation"
        with tempfile.TemporaryDirectory(prefix="clang21-sparse-", dir=args.work_dir) as temporary:
            staging = Path(temporary)
            envelope = staging / "envelope.bin"
            recipe = create_recipe(signed, envelope, args.native_dir, libraries,
                                   args.package_name, args.version_code, certificate)
            reconstructed = staging / "reconstructed.apk"
            reconstruct(recipe, envelope, args.native_dir, reconstructed)
            # Signing-block preservation is verified by the official SDK verifier.
            rebuilt_verification = run(signer_command(args.java, args.apksigner) +
                                       ["verify", "--verbose", "--print-certs", reconstructed])
            require(certificate_from_verification(rebuilt_verification) == certificate,
                    "Reconstructed APK certificate differs")
            destination.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(envelope, destination / "envelope.bin.part")
            (destination / "recipe.json.part").write_text(json.dumps(recipe, indent=2) + "\n", encoding="utf-8")
            (destination / "envelope.bin.part").replace(destination / "envelope.bin")
            (destination / "recipe.json.part").replace(destination / "recipe.json")
        (args.work_dir / "signature-verification.txt").write_text(verification, encoding="utf-8")
        print(json.dumps({"recipe": str(destination / "recipe.json"), "apkBytes": recipe["apk"]["bytes"],
                          "envelopeBytes": recipe["envelope"]["bytes"], "sha256": recipe["apk"]["sha256"],
                          "certificateSha256": certificate, "libraries": len(libraries)}))
    except (ValueError, OSError, subprocess.SubprocessError, zipfile.BadZipFile) as error:
        parser.exit(1, f"Clang 21 installation preparation failed: {error}\n")


if __name__ == "__main__":
    main()
