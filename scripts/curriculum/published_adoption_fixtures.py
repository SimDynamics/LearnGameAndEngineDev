"""Pinned pre-adoption publications for transition tests.

Never derive a historical predecessor by deleting contracts from the live
lesson. These bytes come from an actual Git publication before adoption.
The live lesson is checked independently and is never rewritten by QA.
"""
from functools import lru_cache
import hashlib
import os
from pathlib import Path
import subprocess

import stable_state_layout as state

ROOT = Path(__file__).resolve().parents[2]
BASE = "3a18c2ac0b6e5738f3c56e51e1c794a1b2676384"
PREFIX = "app/src/main/assets/content/es/programming/"
PINS = {
    "programming_practice_tools/comprobaciones_repetibles/lesson_integrar_un_proyecto_y_sostener_su_evidencia.json":
        "48dfff25c51abe88b728c16e8bdd16f21ef4bfd1",
    "programming_practice_tools/evidencia_y_diagnostico/lesson_versiones_del_fuente_y_del_ejecutable.json":
        "04ead481d3d70553bd9a53c418c87db7475a42de",
    "programming_practice_tools/construccion_reproducible/lesson_objetivos_dependencias_y_enlace.json":
        "0d7df509ee5fff9eaf1c7854dfd6baeefa5b37e7",
    "programming_practice_tools/construccion_reproducible/lesson_fuentes_objetivos_y_fases.json":
        "bbce0f144005d4c58b63d2d971573428f25a05fc",
    "programming_practice_tools/construccion_reproducible/lesson_opciones_y_revision_de_la_configuracion.json":
        "6b8cf4381b82723cb14471faed05b53c697e17aa",
    "programming_practice_tools/construccion_reproducible/lesson_cabeceras_y_requisitos_de_uso.json":
        "bfbe625656f2c81f18b89462688523cf6a17ae21",
    "programming_practice_tools/comprobaciones_repetibles/lesson_reorganizar_la_descripcion_conservando_el_comprobador.json":
        "f318736624bac75aae7ee1d8d4498c5586b4e8da",
    "programming_practice_tools/comprobaciones_repetibles/lesson_datos_y_listas_de_objetivos.json":
        "b97aeddac731201415df9005a0778e8487aca0a6",
    "cpp_language_memory_oop/estado_y_ciclo_de_vida_de_clases/lesson_transferir_propiedad_entre_unique_ptr.json":
        "045660d8ce90c199e5dbba4efa1037fd5fc7a4b1",
}


@lru_cache(maxsize=None)
def historical_bytes(path):
    relative = path.relative_to(ROOT).as_posix()
    if not relative.startswith(PREFIX) or relative[len(PREFIX):] not in PINS:
        raise ValueError("No authenticated pre-adoption publication: " + relative)
    expected = PINS[relative[len(PREFIX):]]
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    git = ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}"]
    publication = subprocess.run(
        git + ["show", BASE + ":" + relative, "--"],
        cwd=ROOT, env=environment, capture_output=True)
    if publication.returncode == 0:
        payload = publication.stdout
    else:
        # A shallow checkout may still contain this *exact* immutable blob
        # when Work has not changed the lesson. Never read an unpinned live
        # file or strip declarations to simulate the historical predecessor.
        blob = subprocess.run(git + ["cat-file", "blob", expected],
                              cwd=ROOT, env=environment, capture_output=True)
        if blob.returncode != 0:
            raise ValueError(
                "Missing authenticated pre-adoption Git object for " + relative
                + "; fetch the pinned history before running transition QA")
        payload = blob.stdout
    digest = hashlib.sha1(b"blob " + str(len(payload)).encode("ascii")
                          + b"\0" + payload).hexdigest()
    if digest != expected:
        raise ValueError("Pre-adoption Git blob differs from its pin: " + relative)
    return payload


def before_adoption(path):
    return state.parse_json(historical_bytes(path).decode("utf-8-sig"))


def assert_live_owner_addresses(test, path, owners):
    """Validate live contracts and storage without inventing a new transition."""
    raw = path.read_bytes()
    live = state.parse_json(raw.decode("utf-8-sig"))
    baseline = before_adoption(path)
    old_positions = state.validate(baseline)
    live_positions = state.validate(live)
    test.assertEqual(baseline["id"], live["id"])
    test.assertEqual(baseline["stateLayout"]["family"], live["stateLayout"]["family"])

    def positions(document):
        return {owner: (p, b)
                for p, page in enumerate(document["stateLayout"]["current"])
                for b, owner in enumerate(page["blocks"])}

    old, current = positions(baseline), positions(live)
    for owner in owners:
        with test.subTest(path=path.name, owner=owner):
            test.assertIn(owner, old)
            test.assertIn(owner, current)
            test.assertEqual(old_positions[old[owner]], live_positions[current[owner]])
    test.assertEqual(path.read_bytes(), raw)
    return live
