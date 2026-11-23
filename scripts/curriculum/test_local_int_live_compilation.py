"""Compile and execute authentic local-int C++23 programs with Windows MSVC.

The emitter calls the production cpp_local_int_override implementation, then
this fixture runs the ORIGINAL and ALTERED programs on the host compiler. No
curricular JSON, user account, saved profile or application toolchain is edited.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

import format_batch_history as history
import stable_state_layout as state
from code_format_compat import require_source_lexical, source_projection
from windows_cpp_ci import msvc_cpp23_command


ROOT = Path(__file__).resolve().parents[2]
RELATIVE = ("app/src/main/assets/content/es/programming/cpp_fundamentals/"
            "objetos_y_estado/lesson_enteros_y_aproximaciones.json")
PUBLICATIONS = {
    "original": history.Publication(
        "921e1df9171992ab7a9f1b3803b963b02fd45bbc",
        "47889b1e786f64592d282e760ca729d9c586fcb56d9cbcd4a09133d340d10e06"),
    "formatted": history.Publication(
        "3deb1e829593d5e0afc6bf720a74321bd8bb96c6",
        "8253dc81f582f86d2ab1f7d24558ed68071c1f226d7534eb9913f956235bce5e"),
    "baseline": history.Publication(
        "50b479912dd0e598ad0b717332a15bf7d8606e69",
        "c7aa1134adf373d2c2c9f8f6f77f6423df8fcb6c5cd3e7945c49a7843ddf3b5b"),
}
CONTRACTS = (
    ("u2l5_division_variable", "block_0006_0002",
     "block_0007_0001", "Parte: 4.5 m",
     (("partes", 4, "Parte: 2.25 m"), ("partes", 8, "Parte: 1.125 m"))),
    ("u2l5_suma_agrupada", "p1u2l5_reparar_agrupacion",
     "p1u2l5_referencia_agrupacion", "Parte: 2.75 m",
     (("tramo_adicional_metros", 3, "Parte: 3.75 m"),
      ("partes", 4, "Parte: 1.375 m"))),
)


def publication_payloads() -> dict[str, bytes]:
    payloads = {name: history.publication_bytes(ROOT, RELATIVE, pin)
                for name, pin in PUBLICATIONS.items()}
    payloads["current"] = (ROOT / RELATIVE).read_bytes()
    return payloads


def verify_publications(payloads: dict[str, bytes]) -> dict[str, dict]:
    """Authenticate complete releases in order, without fabricating old lessons."""
    for name, pin in PUBLICATIONS.items():
        history.verify_publication_bytes(payloads[name], pin)
    documents = {name: json.loads(payloads[name])
                 for name in (*PUBLICATIONS, "current")}
    for document in documents.values():
        if (document["id"] != "cpp_fundamentals__objetos_y_estado__enteros_y_aproximaciones"
                or document["stateLayout"]["locale"] != "es"):
            raise ValueError("A U2L5 publication belongs to another lesson or locale")
        state.validate(document)
    # The later semantic choice edit is a separate real publication. Validate
    # each actual step instead of treating the combined changes as one format.
    for earlier, later in zip(documents.values(), list(documents.values())[1:]):
        state.validate_transition(earlier, later)
    original, baseline, current = (documents[name]
                                   for name in ("original", "baseline", "current"))
    old, old_positions = history.active_owners(original)
    active, positions = history.active_owners(current)
    old_storage, storage = state.validate(original), state.validate(current)
    old_meta, baseline_meta, meta = (doc["stateLayout"]
                                    for doc in (original, baseline, current))
    for field in ("origin", "family"):
        if meta[field] != old_meta[field]:
            raise ValueError("Current U2L5 lost its original stable family")
    for field in ("editorial", "sourceFormatRevisions", "retired",
                  "retirementHistory", "choiceRevisions"):
        previous = baseline_meta.get(field, [])
        if meta.get(field, [])[:len(previous)] != previous:
            raise ValueError("Current U2L5 lost already published history: " + field)
    entries = meta.get("sourceFormatRevisions", [])
    archives = {entry["id"]: entry["block"] for entry in entries}
    if len(entries) != len(archives):
        raise ValueError("Current U2L5 has duplicate source-format archives")
    for _, owner, reference, visible, checks in CONTRACTS:
        for identity, expected_type in ((owner, "cpp_compiler"), (reference, "code")):
            if identity not in active or active[identity]["type"] != expected_type:
                raise ValueError("Current U2L5 source or editor retired: " + identity)
            if archives.get(identity) != old[identity]:
                raise ValueError("Current U2L5 lost its complete canonical block: " + identity)
            if storage[positions[identity]] != old_storage[old_positions[identity]]:
                raise ValueError("Current U2L5 changed a historical owner address")
            require_source_lexical(old[identity], active[identity])
            projected = source_projection(active[identity], old[identity])
            if state.state_fingerprint(projected) != state.state_fingerprint(old[identity]):
                raise ValueError("Current U2L5 changed a protected source contract")
        if active[owner]["outputCheck"] != old[owner]["outputCheck"]:
            raise ValueError("Current U2L5 changed its published visible output contract")
        if active[owner]["outputCheck"]["cases"][0]["outputs"] != [visible, visible + "\n"]:
            raise ValueError("Current U2L5 visible output changed: " + owner)
        expected = [{"variable": variable, "replacement": replacement,
                     "outputs": [stdout, stdout + "\n"]}
                    for variable, replacement, stdout in checks]
        for document in (original, current):
            specs = document["compilerLocalIntChecks"]
            by_owner = {item["activity"]: item["probes"] for item in specs}
            if len(specs) != 2 or len(by_owner) != 2 or by_owner.get(owner) != expected:
                raise ValueError("Private U2L5 probe declarations changed: " + owner)
    return documents


def build_command(compiler: str, source: Path, binary: Path) -> list[str]:
    if compiler != "MSVC":
        raise ValueError("Windows MSVC C++23 is required for local-int integration")
    return msvc_cpp23_command(binary, source)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--emitter", required=True, type=Path)
    parser.add_argument("--compiler", required=True, choices=("MSVC",))
    args = parser.parse_args()
    payloads = publication_payloads()
    documents = verify_publications(payloads)
    original = history.active_owners(documents["original"])[0]
    blocks = history.active_owners(documents["current"])[0]
    with tempfile.TemporaryDirectory(prefix="lgaed-local-int-live-") as tmp:
        output = Path(tmp)
        current_input = output / "current-input"
        current_input.mkdir()
        for name, _, reference, _, _ in CONTRACTS:
            (current_input / (name + ".cpp")).write_bytes(blocks[reference]["code"].encode("utf-8"))
        emitted = subprocess.run([str(args.emitter.resolve()), str(output), str(current_input)],
                                 capture_output=True, timeout=30, check=True)
        if b"PASS generated 61" not in emitted.stdout:
            raise RuntimeError("The real source-mutation fixture did not finish")
        sources = sorted(output.glob("*.cpp"))
        if len(sources) != 61:
            raise RuntimeError(f"Expected exactly 61 source programs, got {len(sources)}")
        # Original programs match their complete original publication exactly;
        # the added programs match today's actual physical source, not a
        # canonical projection that could conceal a changed emitted program.
        for name, _, reference, _, _ in CONTRACTS:
            for suffix, authored in (("", original), ("_current", blocks)):
                source = output / (name + suffix + ".cpp")
                if source.read_bytes() != authored[reference]["code"].encode("utf-8"):
                    raise RuntimeError("Generated U2L5 C++ differs from its authentic "
                                       "publication: " + reference + suffix)
        # The two published U2L5 starters are real incomplete student
        # programs, not reference solutions. Build and run each unchanged
        # source on this compiler and compare to its own initial observation.
        # This checks the missing division and the incorrect grouping, while
        # keeping the first-case rubric and the learner's saved source intact.
        rejected_starters = {
            "u2l5_division_variable": b"",
            "u2l5_suma_agrupada": b"Parte: 4.5 m",
        }
        for name, owner, reference, visible, _ in CONTRACTS:
            starter = blocks[owner]["initialCode"]
            if not starter or starter == blocks[reference]["code"]:
                raise RuntimeError(f"U2L5 no longer has an incomplete starter: {owner}")
            initial_source = output / (name + "_initial.cpp")
            initial_source.write_text(starter, encoding="utf-8")
            initial_binary = initial_source.with_suffix(
                ".exe")
            initial_build = subprocess.run(
                build_command(args.compiler, initial_source, initial_binary),
                cwd=output, capture_output=True, timeout=90, check=False)
            if initial_build.returncode:
                raise RuntimeError(
                    f"{args.compiler} cannot compile current U2L5 starter {owner}:\n"
                    + initial_build.stdout.decode(errors="replace")[-4000:]
                    + initial_build.stderr.decode(errors="replace")[-4000:])
            initial_run = subprocess.run(
                [str(initial_binary)], cwd=output,
                capture_output=True, timeout=20, check=False)
            actual = initial_run.stdout.replace(b"\r\n", b"\n")
            required = visible.encode("utf-8")
            if (initial_run.returncode != 0 or initial_run.stderr or
                    actual != rejected_starters[name] or
                    actual in (required, required + b"\n")):
                raise RuntimeError(
                    f"Current U2L5 starter gave wrong initial observation or "
                    f"earned its required output: {owner}, actual={actual!r}, "
                    f"exit={initial_run.returncode}, stderr={initial_run.stderr!r}")
            print(f"PASS local-int MSVC {initial_source.stem}: starter observed and rejected", flush=True)

        accepted = rejected = guarded = 0
        for source in sources:
            evidence = source.with_suffix(".expected").read_bytes()
            kind, separator, expected = evidence.partition(b"\n")
            if not separator or kind not in {b"A", b"R", b"K"}:
                raise RuntimeError(f"Missing or invalid expected stdout: {source.name}")
            binary = source.with_suffix(".exe")
            command = build_command(args.compiler, source, binary)
            compile_run = subprocess.run(command, cwd=output, capture_output=True,
                                         timeout=90, check=False)
            if compile_run.returncode:
                raise RuntimeError(
                    f"{args.compiler} could not compile {source.name}:\n"
                    + compile_run.stdout.decode(errors="replace")[-4000:]
                    + compile_run.stderr.decode(errors="replace")[-4000:])
            observed = subprocess.run([str(binary)], cwd=output,
                                      capture_output=True, timeout=20, check=False)
            if kind == b"K":
                if observed.returncode != 1 or observed.stderr or observed.stdout:
                    raise RuntimeError(
                        f"Unsafe private delta {source.name} escaped its guard: "
                        f"exit={observed.returncode}, stdout={observed.stdout!r}, "
                        f"stderr={observed.stderr!r}")
                guarded += 1
                print(f"PASS local-int MSVC {source.stem}: guarded exit 1", flush=True)
                continue
            if observed.returncode or observed.stderr:
                raise RuntimeError(
                    f"Student program {source.name} terminated abnormally: "
                    f"exit={observed.returncode}, stderr={observed.stderr!r}")
            actual = observed.stdout.replace(b"\r\n", b"\n")
            should_match = kind == b"A"
            if (actual == expected) != should_match:
                raise RuntimeError(
                    f"{source.name}: {'accepted' if should_match else 'rejected'} "
                    f"local-int case did not match its real compiled behavior; "
                    f"actual={actual!r}, expected={expected!r}")
            if should_match:
                accepted += 1
            else:
                rejected += 1
            print(f"PASS local-int MSVC {source.stem}: "
                  f"{'accepted' if should_match else 'rejected'}", flush=True)
        if (accepted, rejected, guarded) != (49, 6, 6):
            raise RuntimeError(f"Incorrect execution counts: {(accepted, rejected, guarded)}")
        if (ROOT / RELATIVE).read_bytes() != payloads["current"]:
            raise RuntimeError("The current U2L5 curriculum changed during isolated QA")
        print(f"PASS {args.compiler} on {sys.platform}: {accepted} positive "
              f"original/probe executions, {rejected} actual hard-coded-copy "
              "counterexamples rejected, 2 opaque no-ops and 2 signed-int boundary overflows blocked; 2 actual incomplete U2L5 starters " 
              "observed; 55 original fixtures preserved plus 6 current-format executions; "
              "63 real C++ compile-and-run cases.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, subprocess.TimeoutExpired, subprocess.CalledProcessError,
            RuntimeError, ValueError) as error:
        print(f"FAIL local-int real compilation: {error}", file=sys.stderr)
        sys.exit(1)
