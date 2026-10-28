"""Check the local Windows narration runtime and its retained resource pack.

Windows currently installs an executable linked to this checkout's source and
runtime.json, rather than an Android AAB. This checks that actual configuration,
without copying assets, loading models, downloading files or modifying caches.
It does not establish playback, curricular listening or portable installation.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import sys

import prepare_english_ai_pack as english_pack


ROOT = Path(__file__).resolve().parents[2]
MAIN_CLASS = "lgaed.narration.windows.WindowsNarrationWorkerKt"
SPACY_ATTRIBUTION_SHA256 = "141446b5c89aca8f7eb9ed2e7c841f35922302378951782b1ee848508e70e7a9"
# Original 32 recordings plus the eight preserved from Android 0d765ea910e2.
# Names are synthesis/cache keys; the second hash verifies the encoded bytes.
PRESERVED_OPUS = {
    "0798f4bd5ab56ddb89b4a2fad21a43e2f47aec5084d1df7c44ece9c4b6cd56b7.ogg": (23923, "de9a35643467541f6e14ac4a609b6231f166f18a6ff4827119a04500ffb2f55d"),
    "09fcddf2081b4fd20060bd189cf9ef7d54ba6f68ba75962d54fa5b6d9f64d6cd.ogg": (12177, "134c8d9c6fb5f565601cbd9a9986364c182f9e0839358e8a269767ee02f13904"),
    "0a65bcfe2a4051acc78a463dab7e557aeda94d8a84c7e76a3ef9ca6a8fce1b41.ogg": (53786, "7fa7d58efaeab28c471ad19facaca16eb0236e3b84d21064abec905e6840b7fb"),
    "0a872e910b1288459c9bf04a4b189be8c3732078670e21e8a3c5d93db258b951.ogg": (10864, "0f1cb9f4d2bac9159ec6d17c17dcccdfca01d0b5a0d4e4ce7d859c8618210566"),
    "0cd3727659366df745e564f58751201ef47fccf4785d9daebaa91974fe8bb40f.ogg": (54677, "d1eb0c364b5e67136980726effef2f09be4758d39efca7f0e51b6bcb204da77e"),
    "225c9115512bf752b40934a2282560e088318174a5550f07868ea9777cc046aa.ogg": (14542, "b7ba51bb44ac35c5e0a63618a85a282453486db874b1fb427652025799c32b01"),
    "2c3077f60d88332d7667a12c1c1608f90c0ade037278c415cbbdf3bde22e2ff4.ogg": (22047, "aced26bf3690eb564020da0150b4607a395830917c144d2e809f88ba95592923"),
    "3274ef68ec9e390cc645b24ce2fe5db7f4ee88cf5e93c67adbb77d1afcdee399.ogg": (17432, "0c8b76031616350640259bebec28c6ff6715f54ff59cf3ea41caf8ead134695c"),
    "3586ee5e373cfc1f70cda7e481bcbdfe28015ac1f52da2a7aa25343d96b2e9e9.ogg": (11375, "cb514af7bb4bd8118c8d60079d92028a15cb7292bc68f8248222b5d27ce3a71f"),
    "3c42322d02e0cc28fdcffb324201f432c3f9e9ea2dd06ac093acb2890a18525c.ogg": (28484, "8c7e8cf438ce2c8b8e43a3b3ed2e49b262d20cb2f8609ea7bf31d3fba2f886f9"),
    "3ffb98490290007930d49742c62744ef2d40f9f40bff87d8a8ab6fc07d18fb98.ogg": (19154, "1e080f9116c84bf0ec8a84a99f16b96a3598f19bf7abee50398b61d8ccd4777e"),
    "411f8dbc57dc9e3e1dbedcdca99995f3c64e9065f02a0d3e99bbd067a4f0fe79.ogg": (5603, "7e2c92b3a94148664c54bc13cb372e29b0548424b3e3363210dd51b8c75531f7"),
    "43fcf3ac48a3094a8031bd97cd429ddfc9b4ab1baa3f46746265c3c0bf40e952.ogg": (19549, "d6107d9429d8fafa41300cff2030a0056e703c65e456c20a226d717681692f9b"),
    "44c21f9eb13de42dfec98378898778d30e88b26ef5aef019b0b8dd1dff8e779d.ogg": (20802, "0acd5de40e6f9a4205173d05ee21863460eb18eb47a3a2e9905bf5da74ac0958"),
    "56d69370ff4a7bb6d8db734dbe8dc48c63b636fe2751c5b900fe901b7b0a0b64.ogg": (11778, "751c4d8f5e5b05d8fa5b108808b63ac1b9d3332bea0a1cbf7dd39cdc0663fa8d"),
    "5e7b60bf026b501f52a8a78c6e73848d58606cc9de2436e6dfd2294fe31f4a8d.ogg": (16764, "6149c543d3533fc53b3441ec70eac25a0d06b4a0e26e2c9158404fcafff67efd"),
    "6a8a96a54c2645ae0dc11bf0a06ea052e116b47e0078097223064e6d54e18789.ogg": (25765, "9596090dccde07fe1a6df1f2dd39ba5a39aaac9779776f4149e47c8722a597d4"),
    "765a6a71c27fc0390e486c0e5639adff4f36b263210a5c84ca4aeda275e714ec.ogg": (9306, "f3125c6d32d40f79f44bb8b7e594052274272baa2b9782cb58ec7454d7a7e6f3"),
    "7764ee034b735d583c4fe9d9208064bea6fa01da256992fe5f7424ce32b76239.ogg": (15035, "971c008a4bd23ba30f857d1d9ef1646be0574f4de37cc1bf4aa740325d16cf11"),
    "80aca6f7ccbcdce797b864784180ecc3a9a50e8e0c74fb6843618b070a2ec6ab.ogg": (19364, "9d3acd5057db1816031aa21e3f52dd92349354e5658033f9db580ff261d33d62"),
    "822bb05d4a6b7948be0aed521bd317958d250cc56884d675cd91498f398c5f87.ogg": (19834, "0a50a13cd5a3d0e17619ed4ed601b7a2b473d531c3b152ecb3b666e77409f93a"),
    "8cceed54b5e127b5ec44c162a24dbae8e7efff529b5c9f394b5d735f0b58ea79.ogg": (11152, "d1fe8fd63632992f06d6ba93d810ab55f89cd7fb140da5a8955b0eba349798ef"),
    "91baa520ae4ecdda3fcf71e97283239dedacd26ed85bbcf191587e9a849e6d45.ogg": (12774, "cadeaa24fa3b5a98f2833adbc6b9a410e5b1a31b64f10a40d52900cd16000d19"),
    "933260abdfa6ff1b47c30023459ba3f09e4c55404b04e005cbd6902ade6f3e8a.ogg": (35653, "075bcde9679801092b63ddcc59250b527afe4f66cfd78afe4137638fa30abbb3"),
    "9c89d50a77a03ed8c5377e320daa4f141a4eccbaf2eb13e77ae4f53ac5e4cac6.ogg": (5382, "32bdbc020ab481992f5ef9308117e7ff22694907a73b905772ffc506f114679b"),
    "9cbaad162999672a5c243f207c699c9b60198b83858c0140d8e65cf425d1a4b3.ogg": (9126, "4aa57282826a4d1caeee12b505419639f0416699a392dbfd47bd6d9c98dea292"),
    "a3341d668056bac3ba3a1b6158132cd69e91114acb698e1dac25da77cf65f82d.ogg": (11109, "7a2b4ec9450bc9efcf86f6f2ed30cbea9ea291a681eda67cf20495fc179e7e65"),
    "a74195de06d1cc80baefa1870a23849758a755e5051e659a315fdd773727968e.ogg": (27306, "2b7709662d93d90074e0791b68e6c53364c196ebd5eedcbc30d43ef7041a31db"),
    "a7500714250d5ea133851ca6fa987e3da4622bbfcaedfad5f368ffee0b1b9258.ogg": (29586, "83481fdde24dcb4fd3d0943a3a9236aef51de954e21b5c599c587d813a8218b1"),
    "abeda911fc132f125358c2600d52cc2740261e0ab8d09f963d3c67269a27a066.ogg": (8111, "cc0bc59c2ecab56779f6e717e5d61af64fc8548d8cc4a6c8f5f926dc051e86a4"),
    "b521e788bfae983c40a6151dc9b6eac3d97d61781f7161202ada10c100c92aab.ogg": (23575, "29e5f43ea66868ecc32f04a2cc268fd0a29fad868c3ac313619b48e296dbb2a4"),
    "b687bf0ddccba047a1b473618783adaf5bf81eaf385e60ea31e784cea5c234d2.ogg": (31334, "9bb20f7e2b79d248ba72820155a03b95fe1f0bb65cb669a375e7ceb364b912d7"),
    "b861331333ed404ab85aede7a060f850459c323eb0cfb4bfa7c36dca8c338a1c.ogg": (5697, "ded9f04daadd7c55cde123a18d6556c20b8b06af176f1e121e899363c4c0525c"),
    "bcb597acd0ebf3b77632064b711ccb1a02e0ad215673f585a7f3685a05426e5f.ogg": (35840, "26370a1a5354868591c2de4d8ec344f04b4c7926361a2862fdb3a439fd162192"),
    "dd66a7611a9ded17ee5a02aebeb3caabb25ae0fdde970a28246c89a7f2651d2f.ogg": (8529, "54dfec401323a61b63df7de6f29c41492d8ab31d08e441aac0714b970b1832be"),
    "e407b7c9828b8f4a07fc53494a44b1c15be57f64da87730af9683dd78af113ec.ogg": (12335, "2361c8a1472649a197a1be3d6913ffa89769ca6264837a3166276d6f659b3c9d"),
    "eaa45562fa28cfe2971da739c20e5716d6748008fd9f3be8a2b5cf6fd043502f.ogg": (24614, "f274cd9c009ae585c3812f10622504ee6c66606b0fde218bf5289569ffecf414"),
    "ed9247d0f7d61fa641b1b621871f693d364f72241c903d018846441a7d552fdc.ogg": (16186, "25e250b18dab7e0ff999a678be19e27b22d3c442aac2a7936d4171fa4d616293"),
    "f1f9d83c463f5545b634c1460c97496fa7fde62aeed027fdd683de470108366b.ogg": (23264, "8f9ef1be222ca6744bdc2f58139ff821d6b347cd39ed1ea3baf5ed2e1b51222f"),
    "fe7524eb8d5ea6da5a66301194c64c970676e9d1390751b4e43c4a278ccf46d4.ogg": (11073, "0956764763a3cecf6cec1d1f82d628db3dc61d69f39794896ee35c722756c9e4"),
}


@dataclass(frozen=True)
class VerificationResult:
    english_artifact_count: int
    english_artifact_bytes: int
    english_metadata_file_count: int
    prerendered_count: int
    prerendered_bytes: int
    preserved_prerendered_count: int


def _path(value: object, field: str) -> Path:
    if not isinstance(value, str) or not value or "\0" in value:
        raise ValueError(f"Missing or invalid runtime path: {field}")
    path = Path(value)
    if not path.is_absolute():
        raise ValueError(f"Runtime path must be absolute: {field}")
    return path


def _same_path(first: Path, second: Path) -> bool:
    return os.path.normcase(str(first.resolve())) == os.path.normcase(str(second.resolve()))


def _regular_file(path: Path, field: str) -> None:
    if not path.is_file() or path.is_symlink():
        raise ValueError(f"Missing or symlinked Windows runtime file: {field}: {path}")


def verify_prerendered(assets: Path) -> tuple[int, int]:
    """Verify preserved audio byte-exactly, allowing future additional clips."""
    directory = assets / "narration/prerendered"
    for name, (size, expected_hash) in PRESERVED_OPUS.items():
        path = directory / name
        _regular_file(path, "preserved Opus")
        if path.stat().st_size != size or hashlib.sha256(path.read_bytes()).hexdigest() != expected_hash:
            raise ValueError(f"Preserved Opus size/SHA-256 mismatch: {name}")
    clips = sorted(directory.glob("*.ogg"))
    for path in clips:
        _regular_file(path, "prerendered Opus")
        if not re.fullmatch(r"[0-9a-f]{64}\.ogg", path.name):
            raise ValueError(f"Invalid narration cache key: {path.name}")
        with path.open("rb") as source:
            header = source.read(64)
        if not header.startswith(b"OggS") or b"OpusHead" not in header:
            raise ValueError(f"Prerendered audio is not Ogg Opus: {path.name}")
    return len(clips), sum(path.stat().st_size for path in clips)


def verify_persistent_attribution(repo_root: Path, english_assets: Path) -> None:
    path = repo_root / "app/src/main/res/raw/spacy_pos_attribution.txt"
    _regular_file(path, "persistent spaCy attribution")
    notice = path.read_bytes().replace(b"\r\n", b"\n")
    lookup = (english_assets / "licenses/SPACY-LOOKUPS-MIT.txt").read_bytes().replace(b"\r\n", b"\n")
    if not lookup.strip() or lookup.strip() not in notice:
        raise ValueError("Persistent spaCy attribution must retain the full lookups MIT notice")
    if hashlib.sha256(notice).hexdigest() != SPACY_ATTRIBUTION_SHA256:
        raise ValueError("Persistent spaCy attribution differs from the retained original and lookups credits")


def verify_runtime(runtime_path: Path, *, repo_root: Path = ROOT) -> VerificationResult:
    """Verify a generated config, including a pending .json.part before publish.

    Kokoro model/original-WAV folders may be absent: preserved Opus cache hits
    remain usable before synthesis is available. Their configured argument paths
    must still agree. An explicit English assets override is allowed when its
    eight resources, provenance and licenses satisfy the same audited contract.
    """
    runtime_path, repo_root = Path(runtime_path), Path(repo_root).resolve()
    _regular_file(runtime_path, "configuration")
    config = json.loads(runtime_path.read_text(encoding="utf-8-sig"))
    if not isinstance(config, dict) or config.get("mainClass") != MAIN_CLASS:
        raise ValueError("Unsupported Windows narration main class")
    paths = {name: _path(config.get(name), name) for name in (
        "java", "javaExecutable", "jvm", "assets", "resources", "englishResources", "originalWavs")}
    for name in ("java", "javaExecutable", "jvm"):
        _regular_file(paths[name], name)
    classpath = config.get("classpath")
    if not isinstance(classpath, str) or not classpath:
        raise ValueError("Missing Windows narration classpath")
    for entry in classpath.split(os.pathsep):
        _regular_file(_path(entry, "classpath"), "classpath")
    arguments = config.get("arguments")
    if (not isinstance(arguments, list) or not all(isinstance(arg, str) for arg in arguments)
            or len(arguments) < 3 or (len(arguments) - 3) % 2):
        raise ValueError("Invalid Windows narration argument pairs")
    if not _same_path(_path(arguments[0], "JVM argument"), paths["jvm"]):
        raise ValueError("JVM argument differs from runtime field")
    if arguments[1] != classpath or arguments[2] != MAIN_CLASS:
        raise ValueError("Classpath or main class argument differs from runtime field")
    flags = {}
    for index in range(3, len(arguments), 2):
        key, value = arguments[index:index + 2]
        if not key.startswith("--") or key in flags:
            raise ValueError(f"Invalid or duplicate Windows narration argument: {key}")
        flags[key] = value
    for flag, field in (("--assets", "assets"), ("--resources", "resources"),
                        ("--english-resources", "englishResources"), ("--original-wavs", "originalWavs")):
        if not _same_path(_path(flags.get(flag), flag), paths[field]):
            raise ValueError(f"{flag} argument differs from runtime field")
    expected_profile = "Work" if repo_root.name.endswith("-Work") else "Codex"
    if flags.get("--profile") != expected_profile:
        raise ValueError("Narration profile differs from this checkout")
    ffmpeg = config.get("ffmpeg", "")
    if not isinstance(ffmpeg, str) or flags.get("--ffmpeg", "") != ffmpeg:
        raise ValueError("FFmpeg argument differs from runtime field")
    if ffmpeg:
        _regular_file(_path(ffmpeg, "ffmpeg"), "ffmpeg")
    if not _same_path(paths["assets"], repo_root / "app/src/main/assets"):
        raise ValueError("Narration assets must belong to this checkout")
    manifest = repo_root / "app/src/main/java/com/learngraphicsprogramming/app/narration/EnglishAuxResources.kt"
    verified = english_pack.verify_staged(paths["englishResources"], manifest=manifest)
    verify_persistent_attribution(repo_root, paths["englishResources"])
    count, size = verify_prerendered(paths["assets"])
    return VerificationResult(verified.artifact_count, verified.total_artifact_bytes,
                              verified.metadata_file_count, count, size, len(PRESERVED_OPUS))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, default=ROOT / "out/narration/runtime.json")
    args = parser.parse_args()
    try:
        result = verify_runtime(args.runtime)
    except (ValueError, OSError) as error:
        print(f"Windows narration verification failed: {error}", file=sys.stderr)
        return 1
    print(f"Windows narration: {result.english_artifact_count} English resources "
          f"({result.english_artifact_bytes} bytes), {result.english_metadata_file_count} metadata files, "
          f"{result.prerendered_count} Ogg Opus ({result.prerendered_bytes} bytes); "
          "retained spaCy attribution verified. Playback/listening is a separate check.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
