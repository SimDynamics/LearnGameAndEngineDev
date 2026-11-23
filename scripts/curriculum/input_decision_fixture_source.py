"""Eight complete input-decision publications and isolated Windows QA plans.

BEFORE is the exact pinned Git publication. AFTER adds compilerInputChecks only;
all public cases, references, existing local checks and storage identities remain
unchanged. Literal and invariant counterexamples are QA programs, not curriculum.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import subprocess

import stable_state_layout as state
from diagnostic_fixture_source import checked_output, existing_matches


ROOT = Path(__file__).resolve().parents[2]
PUBLISHED_COMMIT = "08a11877a2d2c0bf96a846660e3baea69e53f758"
PUBLICATIONS = {'u3l3': {'path': 'app/src/main/assets/content/es/programming/cpp_fundamentals/expresiones_y_decisiones/lesson_comparaciones_y_condiciones.json',
          'sha256': '9c43940696d0fda803f49378e93250d0746c5009bde1050d119e37487c9ab308',
          'blobSha': 'd3fc63c5f1f1088b530983bd271b85f16cd6e733',
          'activity': 'p1u3l3_construir_limite_peso',
          'references': {'p1u3l3_reparar_humedad': 'p1u3l3_ejemplo_humedad',
                         'p1u3l3_construir_limite_peso': 'p1u3l3_ejemplo_limite_peso'},
          'referenceSha256': {'p1u3l3_reparar_humedad': '6f3512b2c92d8fdf7fc3dbcb7ccb81e4ee4f31e2672e522d80392a0465459a24',
                              'p1u3l3_construir_limite_peso': '9b7d9e54d0cbe798006d55a485c862230ab3ddce949050b47b9b3522cf021ad2'},
          'publicCases': (('20 20', 'Admitido: 1\nEn el máximo: 1'),
                          ('19 20', 'Admitido: 1\nEn el máximo: 0'),
                          ('21 20', 'Admitido: 0\nEn el máximo: 0'),
                          ('20 18', 'Admitido: 0\nEn el máximo: 0')),
          'privateInputs': ('19 20', '21 20', '20 18')},
 'u3l4': {'path': 'app/src/main/assets/content/es/programming/cpp_fundamentals/expresiones_y_decisiones/lesson_alternativas_y_fronteras.json',
          'sha256': 'b1c1a2400861c1ed7348de8caeb15b49fb30cfe07341bf82839ec898ea699f70',
          'blobSha': '7e955a9c87ffb7b0db4ef7eea69d04323114f0e9',
          'activity': 'p1u3l4_construir_alerta',
          'references': {'p1u3l4_reparar_alerta': 'p1u3l4_ejemplo_reparacion',
                         'p1u3l4_construir_alerta': 'p1u3l4_ejemplo_lectura'},
          'referenceSha256': {'p1u3l4_reparar_alerta': '1ce44a312c9d4ef078ee90aaa4924991b4f163e0386ea87d75c38ef92020a143',
                              'p1u3l4_construir_alerta': '461c88355bac2a3a0e646f982c14bbb0f1bae406e3c4b0b0df8af69ffc9722d1'},
          'publicCases': (('3 4', 'Normal\nMarca: 0'),
                          ('4 4', 'Alerta\nMarca: 1'),
                          ('5 4', 'Alerta\nMarca: 1'),
                          ('3 3', 'Alerta\nMarca: 1')),
          'privateInputs': ('4 4', '5 4', '3 3')},
 'u3l5': {'path': 'app/src/main/assets/content/es/programming/cpp_fundamentals/expresiones_y_decisiones/lesson_combinar_condiciones.json',
          'sha256': 'd5d696e9f88ccdaa9b997d00bcd93c677f8e6af861eaef82e9409d3374725c67',
          'blobSha': 'c0e67f1c87127e974decb0396bc3adfe33526a9a',
          'activity': 'p1u3l5_construir_acceso',
          'references': {'p1u3l5_reparar_parentesis': 'p1u3l5_ejemplo_reparacion',
                         'p1u3l5_construir_acceso': 'p1u3l5_ejemplo_acceso'},
          'referenceSha256': {'p1u3l5_reparar_parentesis': 'f26acbb7524bf3761fd46912e517228359308ce4c50946a783f291db2d772d45',
                              'p1u3l5_construir_acceso': 'f48459e28178c9d73f92c617a8a637fbe3fe9a1e3805f245ad7978935a6f0e4d'},
          'publicCases': (('1 0 0', 'Entrada denegada'),
                          ('0 0 0', 'Entrada denegada'),
                          ('0 0 1', 'Entrada denegada'),
                          ('0 1 0', 'Entrada denegada'),
                          ('0 1 1', 'Entrada permitida'),
                          ('1 0 1', 'Entrada permitida'),
                          ('1 1 0', 'Entrada denegada'),
                          ('1 1 1', 'Entrada permitida')),
          'privateInputs': ('0 0 0', '0 0 1', '0 1 0', '0 1 1', '1 0 1', '1 1 0', '1 1 1'),
          'allowSameOutput': True},
 'u3l6': {'path': 'app/src/main/assets/content/es/programming/cpp_fundamentals/expresiones_y_decisiones/lesson_validez_y_cortocircuito.json',
          'sha256': '9d7ca6b5d82b2dac1809ab01b8b308f9e81027c4c4c6ce796502b42cda7ca8ea',
          'blobSha': 'c126b16c872d80f85f20c41d7636d9240d911e1d',
          'activity': 'p1u3l6_construir_reparto',
          'references': {'p1u3l6_reparar_resto': 'p1u3l6_ejemplo_resto',
                         'p1u3l6_construir_reparto': 'p1u3l6_ejemplo_reparto'},
          'referenceSha256': {'p1u3l6_reparar_resto': '134ba6d6c94ba286a38d7db970af6df951668d3dee629225c977364c9561bbce',
                              'p1u3l6_construir_reparto': 'cc7b0a8c1bd48f6066cbbc53077ee5e28a2df63c672f16289a13a4b9422e1224'},
          'publicCases': (('28 0', '0\nCapacidad invalida'),
                          ('28 -1', '0\nCapacidad invalida'),
                          ('28 4', '1\nGrupos: 7'),
                          ('28 6', '0\nGrupos: 4'),
                          ('0 4', '1\nGrupos: 0'),
                          ('1 4', '0\nGrupos: 0'),
                          ('120 12', '1\nGrupos: 10')),
          'privateInputs': ('28 -1', '28 4', '28 6', '0 4', '1 4', '120 12'),
          'allowSameOutput': True},
 'u3l7': {'path': 'app/src/main/assets/content/es/programming/cpp_fundamentals/expresiones_y_decisiones/lesson_decisiones_compuestas_y_pruebas.json',
          'sha256': 'adc084181b89193086d4c3e121587738d93f9d644a53841ddde31333097b7342',
          'blobSha': 'c696794674dd0ad4c46a14c7873a503341e32a22',
          'activity': 'p1u3l7_construir_despacho',
          'references': {'p1u3l7_reparar_prioridad': 'p1u3l7_ejemplo_prioridad',
                         'p1u3l7_construir_despacho': 'p1u3l7_ejemplo_despacho'},
          'referenceSha256': {'p1u3l7_reparar_prioridad': 'a85918362e67e3d45a7efefc767903b791f77e5dcd1ecdbe27a37acae5a97582',
                              'p1u3l7_construir_despacho': '3c308f18b52b76c767c984a36517955fbe57e29dd6d83978c18ec622bc4eda58'},
          'publicCases': (('0 0', 'Cantidad invalida'),
                          ('6 1', 'Pendiente: 2'),
                          ('0 1', 'Cantidad invalida'),
                          ('13 1', 'Cantidad invalida'),
                          ('1 1', 'Completo'),
                          ('4 1', 'Completo'),
                          ('5 1', 'Pendiente: 1'),
                          ('12 1', 'Pendiente: 8'),
                          ('6 0', 'Sin permiso')),
          'privateInputs': ('6 1', '0 1', '13 1', '1 1', '4 1', '5 1', '12 1', '6 0'),
          'allowSameOutput': True},
 'u3l8': {'path': 'app/src/main/assets/content/es/programming/cpp_fundamentals/expresiones_y_decisiones/lesson_construir_decisiones_desde_un_problema.json',
          'sha256': '2d0756fe639c3591e8db77ffe3a085a7a7ff8a49e24e0175767f6df5d4b1640c',
          'blobSha': '3cf7a7eda9332489c83a7d0b9ab41ed52b209cd1',
          'activity': 'p1u3l8_cambiar_minimo',
          'references': {'block_0002_0002': 'p1u3l8_ejemplo_original',
                         'p1u3l8_cambiar_minimo': 'p1u3l8_ejemplo_minimo'},
          'referenceSha256': {'block_0002_0002': 'c5fd7a64eb3bd0f22483785df935b8de1d29eef4aea650a59747549f22e50432',
                              'p1u3l8_cambiar_minimo': '54e4a8474a29ec26a6248c754a78e45baba97fb0d7cf71941a7990c12160fd6e'},
          'publicCases': (('0 4 1', 'Datos invalidos'),
                          ('14 4 1', 'Grupos: 3\nSobrantes: 2'),
                          ('14 4 0', 'Reparto pendiente'),
                          ('12 4 1', 'Grupos: 3'),
                          ('12 4 0', 'Grupos: 3'),
                          ('30 6 1', 'Grupos: 5'),
                          ('-1 4 1', 'Datos invalidos'),
                          ('31 4 1', 'Datos invalidos'),
                          ('12 0 1', 'Datos invalidos'),
                          ('12 7 1', 'Datos invalidos'),
                          ('0 0 1', 'Datos invalidos'),
                          ('1 4 1', 'Grupos: 0\nSobrantes: 1'),
                          ('1 4 0', 'Reparto pendiente')),
          'privateInputs': ('14 4 1', '14 4 0', '12 4 1', '12 4 0', '30 6 1', '1 4 1', '1 4 0')},
 'u4l1': {'path': 'app/src/main/assets/content/es/programming/cpp_fundamentals/repeticion_y_terminacion/lesson_condicion_cuerpo_y_progreso.json',
          'sha256': 'f9538d2daa7c65ef5e94c58e5527fb75f4bd0855fb057e02de2e3cfa173a961b',
          'blobSha': '070901730bdb96a6a843bbb6d1c9c5a4b630f828',
          'activity': 'p1u4l1_construir_recorrido',
          'references': {'p1u4l1_reparar_limite': 'p1u4l1_ejemplo_limite',
                         'p1u4l1_construir_recorrido': 'p1u4l1_ejemplo_recorrido'},
          'referenceSha256': {'p1u4l1_reparar_limite': '789b53cccd2222a0a0aac62115b8da48637648c41245e5d933e8f48faa29f068',
                              'p1u4l1_construir_recorrido': 'd95f980384f62f705e08732defaeb60cd4ef91f9e889c3d6f37040f1c73b2c9e'},
          'publicCases': (('0', '0:2:4:Fin'), ('1', '1:3:Fin'), ('4', '4:Fin'), ('5', 'Fin')),
          'privateInputs': ('1', '4', '5')},
 'u4l2': {'path': 'app/src/main/assets/content/es/programming/cpp_fundamentals/repeticion_y_terminacion/lesson_for_y_limites_del_recorrido.json',
          'sha256': '435a7f8f740f41e1696e3e5807fd7e4fc5fc03a814943b0b8257c4d6b2b76074',
          'blobSha': 'cb2970dbd58b07060f80bb3baaad3fcce0181cf7',
          'activity': 'p1u4l2_construir_descendente',
          'references': {'p1u4l2_reparar_inclusion': 'p1u4l2_ejemplo_inclusion',
                         'p1u4l2_construir_descendente': 'p1u4l2_ejemplo_descendente'},
          'referenceSha256': {'p1u4l2_reparar_inclusion': 'b3d00cd5ab39db46bd7bcfc61c2b253c131fd014c237d9bcabe14800b2f245f1',
                              'p1u4l2_construir_descendente': '750ececeed805b0a28796eb40f263e369ea0bce9e862b7786f79496f827757ad'},
          'publicCases': (('4', 'Puesto 4\nPuesto 3\nPuesto 2\nFin'),
                          ('5', 'Puesto 5\nPuesto 4\nPuesto 3\nPuesto 2\nFin'),
                          ('2', 'Puesto 2\nFin'),
                          ('1', 'Fin')),
          'privateInputs': ('5', '2', '1')}}

# Each negative passes the first public case and every contrasting private case.
# Only the listed equal-output observations reject it; Windows QA checks this.
INVARIANT_COUNTEREXAMPLES = {'u3l5': ({'label': 'open_only',
           'previous': '(tiene_pase || tiene_invitacion) && sala_abierta',
           'replacement': 'sala_abierta',
           'occurrences': 1,
           'sourceSha256': '08617cb383f53753e637affad2ab7109d3625edd576c953c083dbe7860af0c67',
           'failedInputs': ('0 0 1',)},
          {'label': 'invitation_ignores_closure',
           'previous': '(tiene_pase || tiene_invitacion) && sala_abierta',
           'replacement': '(tiene_pase && sala_abierta) || tiene_invitacion',
           'occurrences': 1,
           'sourceSha256': '1dbdbfa204c1a982a6da4c11424d61742340f4dbc679d12667f3c3361ca7cf9c',
           'failedInputs': ('0 1 0', '1 1 0')}),
 'u3l6': ({'label': 'nonzero_instead_of_positive',
           'previous': 'capacidad > 0',
           'replacement': 'capacidad != 0',
           'occurrences': 2,
           'sourceSha256': '71c3cf883440d0a83aa6a0c3f781a64eba22a7d6ef9acec6cbef237260c17c30',
           'failedInputs': ('28 -1',)},),
 'u3l7': ({'label': 'no_upper_bound',
           'previous': '(solicitadas < 1) || (solicitadas > 12)',
           'replacement': 'solicitadas < 1',
           'occurrences': 1,
           'sourceSha256': '51f2ddd82a9e9ac603678b4ad301d7f928a05f8de377e9cb5bc07d0e0fa1f481',
           'failedInputs': ('13 1',)},
          {'label': 'zero_allowed_with_permission',
           'previous': '(solicitadas < 1) || (solicitadas > 12)',
           'replacement': '((solicitadas < 1) && !permiso) || (solicitadas > 12)',
           'occurrences': 1,
           'sourceSha256': 'ab162394020c8eef4bdcffc6a9ee0dbab8db87ed6d7067f028204c15215722f3',
           'failedInputs': ('0 1',)})}


def sha256(payload):
    return hashlib.sha256(payload).hexdigest()


def json_bytes(document):
    return (json.dumps(document, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")


def alternatives(stdout):
    return [stdout, stdout + "\n"]


def public_cases(name):
    return [{"standardInput": stdin, "outputs": alternatives(stdout)}
            for stdin, stdout in PUBLICATIONS[name]["publicCases"]]


def declarations(name):
    publication = PUBLICATIONS[name]
    cases = {case["standardInput"]: case for case in public_cases(name)}
    check = {"activity": publication["activity"], "probes": [
        deepcopy(cases[stdin]) for stdin in publication["privateInputs"]]}
    # The historical contract remains byte-compatible when the option is absent.
    if publication.get("allowSameOutput"):
        check["allowSameOutput"] = True
    return [check]


def verify_published_bytes(name, payload):
    publication = PUBLICATIONS[name]
    blob = hashlib.sha1(b"blob " + str(len(payload)).encode("ascii") + b"\0" + payload).hexdigest()
    if sha256(payload) != publication["sha256"] or blob != publication["blobSha"]:
        raise ValueError(f"Input-decision QA differs from its complete pinned publication: {name}")
    return payload


def published_bytes(name):
    publication = PUBLICATIONS[name]
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    reference = f"{PUBLISHED_COMMIT}:{publication['path']}"
    try:
        payload = subprocess.check_output(
            ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show", reference],
            cwd=ROOT, env=environment, stderr=subprocess.PIPE)
    except (OSError, subprocess.CalledProcessError) as error:
        raise RuntimeError(f"Missing local input-decision publication {reference}; "
                           "no fetch or current-asset fallback is permitted") from error
    return verify_published_bytes(name, payload)


def fixed_output_source(stdout):
    # Input checks constrain behavior, not variable names or statement layout.
    # Replacing cout chains inside a conditional or loop could change the first
    # public result, so this negative is a separate, minimal literal program.
    return ("#include <iostream>\n\nint main() {\n    std::cout << " +
            json.dumps(stdout, ensure_ascii=False) + ";\n    return 0;\n}\n")


def invariant_counterexamples(name, reference):
    result = []
    for negative in INVARIANT_COUNTEREXAMPLES.get(name, ()):
        if reference.count(negative["previous"]) != negative["occurrences"]:
            raise ValueError(f"The exact invariant counterexample source changed: {name}/{negative['label']}")
        source = reference.replace(negative["previous"], negative["replacement"])
        if source == reference or sha256(source.encode("utf-8")) != negative["sourceSha256"]:
            raise ValueError(f"The invariant counterexample differs from its authenticated mutation: {name}/{negative['label']}")
        result.append({"label": negative["label"], "source": source,
                       "sourceSha256": negative["sourceSha256"],
                       "failedInputs": list(negative["failedInputs"])})
    return result


def lesson_payloads(name):
    publication = PUBLICATIONS[name]
    original = published_bytes(name)
    before = state.parse_json(original.decode("utf-8"))
    state.validate(before)
    if "compilerInputChecks" in before:
        raise ValueError(f"The exact input-decision publication already has input checks: {name}")
    blocks = state._blocks(before)
    references = publication["references"]
    compilers = {identity for identity, block in blocks.items()
                 if block.get("type") == "cpp_compiler"}
    if len(compilers) != 2 or compilers != set(references):
        raise ValueError(f"Input-decision QA must retain both original editors: {name}")
    activity = publication["activity"]
    owner = blocks[activity]
    cases = public_cases(name)
    expected = {"inputComparison": "tokens", "cases": cases}
    if (not before.get("compilerPuzzlesRequired") or
            owner.get("initialStandardInput") != cases[0]["standardInput"] or
            owner.get("outputCheck") != expected):
        raise ValueError(f"Input-decision QA changed the exact prepared input or public cases: {name}")

    authored = {entry["activity"]: entry["blocks"] for entry in before["referenceAfter"]}
    solutions = []
    for identity, reference in references.items():
        block = blocks[reference]
        if (reference not in authored.get(identity, ()) or block.get("type") != "code"
                or block.get("language") != "cpp"):
            raise ValueError(f"Input-decision QA lost an authentic reference: {name}/{identity}")
        source = block["code"]
        digest = sha256(source.encode("utf-8"))
        if digest != publication["referenceSha256"][identity]:
            raise ValueError(f"Input-decision QA reference bytes changed: {name}/{identity}")
        solutions.append({"activity": identity, "source": source, "sourceSha256": digest})

    after = deepcopy(before)
    after["compilerInputChecks"] = declarations(name)
    state.validate_transition(before, after)
    preserved = deepcopy(after)
    del preserved["compilerInputChecks"]
    if preserved != before:
        raise ValueError(f"Input-decision QA changed an existing field: {name}")

    reference = references[activity]
    fixed = fixed_output_source(cases[0]["outputs"][0])
    negatives = invariant_counterexamples(name, blocks[reference]["code"])
    private = {probe["standardInput"]: probe for probe in after["compilerInputChecks"][0]["probes"]}
    first_outputs = set(cases[0]["outputs"])
    for negative in negatives:
        failed_inputs = negative["failedInputs"]
        if (not failed_inputs or len(failed_inputs) != len(set(failed_inputs)) or
                any(stdin not in private or not (first_outputs & set(private[stdin]["outputs"]))
                    for stdin in failed_inputs)):
            raise ValueError(f"Invariant counterexample lost its equal-output observation: {name}/{negative['label']}")
    plan = {"activity": activity, "reference": reference,
            "fixedSource": fixed, "fixedSourceSha256": sha256(fixed.encode("utf-8")),
            "inputComparison": "tokens", "publicCases": deepcopy(cases),
            "publicCasesSha256": sha256(json_bytes(cases)),
            "invariantCounterexamples": negatives}
    before_name, after_name = f"{name}/before.json", f"{name}/after.json"
    revised = json_bytes(after)
    return {before_name: original, after_name: revised}, {
        "id": name, "before": before_name, "after": after_name,
        "beforeSha256": sha256(original), "afterSha256": sha256(revised),
        "solutions": solutions, "plans": [plan],
    }


def export_payloads():
    payloads, lessons = {}, []
    for name in PUBLICATIONS:
        files, lesson = lesson_payloads(name)
        payloads.update(files)
        lessons.append(lesson)
    plans = [plan for lesson in lessons for plan in lesson["plans"]]
    if (len(lessons) != 8 or sum(len(lesson["solutions"]) for lesson in lessons) != 16 or
            len(plans) != 8 or sum(len(plan["publicCases"]) for plan in plans) != 53 or
            sum(len(declarations(name)[0]["probes"]) for name in PUBLICATIONS) != 40 or
            sum(len(plan["invariantCounterexamples"]) for plan in plans) != 5):
        raise ValueError("Input-decision QA requires 8 lessons, 16 solutions, 8 owners, "
                         "53 public cases, 40 private executions and 5 invariant contrasts")
    payloads["manifest.json"] = json_bytes({
        "version": 1, "kind": "input-decision", "publishedCommit": PUBLISHED_COMMIT,
        "lessons": lessons,
    })
    return payloads


def export(output_dir):
    output = checked_output(output_dir)
    payloads = export_payloads()
    # Authenticate every publication and inspect every destination before writes.
    for name, payload in payloads.items():
        existing_matches(output / name, payload)
    for name, payload in payloads.items():
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        checked_output(target.parent)
        if existing_matches(target, payload):
            continue
        try:
            with target.open("xb") as stream:
                stream.write(payload)
        except FileExistsError:
            if not existing_matches(target, payload):
                raise
    return output / "manifest.json"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    arguments = parser.parse_args()
    print(export(arguments.output_dir))
