"""Authoring contract for deterministic, ungraded curricular simulations.

The Windows engine derives each frame from the declared input and model. This
module validates the same bounded data; it never interprets C++, evaluates a
template as code, opens learner state or changes a curricular document.
"""
from __future__ import annotations

import json
import math
import re


FIELDS = frozenset({"type", "id", "title", "description", "simulation"})
SPEC_FIELDS = frozenset({
    "version", "model", "view", "valueType", "values", "sort", "editable",
    "labels", "explanations", "outputTemplate", "scenarios",
})
LABEL_FIELDS = frozenset({"array", "start", "end", "frequency", "maximum", "leaders", "output"})
EXPLANATION_FIELDS = frozenset({
    "initial", "start", "advance", "newMaximum", "tie", "belowMaximum", "done", "empty",
})
VIEWS = frozenset({"array", "groups", "walkthrough", "candidates", "summary"})
TEMPLATE_FIELDS = frozenset({
    "value", "start", "end", "frequency", "maximum", "leaders", "size", "group", "groups",
    "countedIndex", "originIndex", "processedCount", "remainingCount", "previousMaximum",
})
MAX_VALUES = 64
MAX_SCENARIOS = 8
MAX_TRACE_BYTES = 8 * 1024 * 1024
_SCENARIO_ID = re.compile(r"[A-Za-z_][A-Za-z0-9_-]{0,63}\Z")

# Keep the source bytes unchanged. Only the JSON editor and counted/narrated
# labels escape Unicode separators and invisible formatting characters.
# This table mirrors invisible_text_codepoint in native/core/simulation.cpp.
# Blank-looking Hangul fillers and the Braille blank are real labels: the
# native model escapes them as \uXXXX, so count six ASCII bytes rather
# than the three UTF-8 bytes when calculating the 8 MiB frame budget.
_INVISIBLE_TEXT_POINTS = frozenset(
    set(range(0x7f, 0xa1)) |
    {0xad, 0x34f, 0x61c, 0x1680, 0x205f, 0x3000, 0xfeff,
     0x115f, 0x1160, 0x2800, 0x3164, 0xffa0}
    | set(range(0x17b4, 0x17b6)) | set(range(0x180b, 0x1810))
    | set(range(0x1bca0, 0x1bca4)) | set(range(0x1d173, 0x1d17b))
    | set(range(0x2000, 0x2010)) | set(range(0x2028, 0x2030))
    | set(range(0x2060, 0x2070))
    | set(range(0xfe00, 0xfe10)) | set(range(0xe0100, 0xe01f0))
)


def _json_invisible_escape(point):
    if point > 0xffff:
        offset = point - 0x10000
        return f"\\u{0xd800 + (offset >> 10):04x}\\u{0xdc00 + (offset & 0x3ff):04x}"
    return f"\\u{point:04x}"


_INVISIBLE_TEXT_ESCAPES = {point: _json_invisible_escape(point)
                           for point in _INVISIBLE_TEXT_POINTS}



def _require(condition, message):
    if not condition:
        raise ValueError(message)


def _text(value, field, maximum, *, nonblank=True, single_line=False, allow_empty=False):
    empty_requirement = "" if allow_empty else " no vacío"
    _require(type(value) is str and (allow_empty or bool(value)) and "\0" not in value,
             f"{field} requiere texto{empty_requirement} sin NUL")
    try:
        encoded = value.encode("utf-8", errors="strict")
    except UnicodeError as error:
        raise ValueError(f"{field} contiene Unicode no válido") from error
    _require(len(encoded) <= maximum, f"{field} supera {maximum} bytes UTF-8")
    _require(not nonblank or bool(value.strip()), f"{field} no puede contener sólo espacios")
    _require(not single_line or ("\n" not in value and "\r" not in value),
             f"{field} no admite CR ni LF dentro de un valor")
    return encoded


def _integer(value, minimum, maximum):
    # WinRT JSON exposes numeric values, not the spelling 1 / 1.0 / 1e0.
    return (type(value) in (int, float) and minimum <= value <= maximum
            and (type(value) is int or (math.isfinite(value) and value.is_integer())))


def validate_template(value, field, maximum, *, counting_explanation=True):
    """Accept only substitutions meaningful in this phase of the simulation."""
    _text(value, field, maximum)
    position = 0
    while position < len(value):
        if value[position] == "{":
            end = value.find("}", position + 1)
            _require(end >= 0 and value[position + 1:end] in TEMPLATE_FIELDS,
                     f"{field} contiene una sustitución no admitida o llaves incompletas")
            _require(counting_explanation or value[position + 1:end] not in {
                "countedIndex", "originIndex"},
                f"{field} no puede usar un índice de elemento recién contado")
            position = end + 1
        else:
            _require(value[position] != "}", f"{field} contiene una llave de cierre sin apertura")
            position += 1


def _values(values, spec, field):
    _require(type(values) is list and len(values) <= MAX_VALUES,
             f"{field} requiere entre 0 y {MAX_VALUES} valores")
    if spec["valueType"] == "integer":
        _require(all(_integer(value, -(2 ** 31), 2 ** 31 - 1) for value in values),
                 f"{field} sólo admite enteros numéricos de 32 bits, sin booleanos")
        keys = values
    else:
        keys = [_text(value, f"{field}[{index}]", 128, nonblank=False, single_line=True, allow_empty=True)
                for index, value in enumerate(values)]
    _require(spec["sort"] or all(left <= right for left, right in zip(keys, keys[1:])),
             f"{field} debe estar ordenado de menor a mayor cuando sort es false")


def _trace_text_bytes(spec, values):
    """Count the exact native frame text budget without retaining a trace.

    Each frame counts its explanation and complete accumulated output. Values
    substituted into a template are data: braces within them are never parsed
    again. Explanations show quoted text labels; output preserves the raw data.
    This bounded calculation validates each authored scenario separately.
    """
    source_indices = list(range(len(values)))
    if spec["sort"]:
        # Sorting indices is stable; repeated values retain their distinct
        # original positions. Match the native core's single permutation.
        source_indices.sort(key=(lambda index: values[index].encode("utf-8"))
                            if spec["valueType"] == "text" else
                            (lambda index: values[index]))
    ordered = [values[index] for index in source_indices]
    groups = []
    for index, value in enumerate(ordered):
        if not groups or groups[-1][2] != value:
            groups.append([index, index + 1, value])
        else:
            groups[-1][1] = index + 1
    total, maximum, leaders, output = 0, 0, [], ""

    def value_text(value, *, display=False):
        if spec["valueType"] == "integer":
            return str(int(value))
        if not display:
            return value
        rendered = json.dumps(value, ensure_ascii=False).translate(_INVISIBLE_TEXT_ESCAPES)
        if not value:
            return rendered + " (cadena vacía)"
        if all(character == " " for character in value):
            return rendered + (" (1 espacio)" if len(value) == 1 else f" ({len(value)} espacios)")
        return rendered

    def context(begin, end, group, frequency, value=None, *, display=False,
                counted_at=None, previous_maximum=None):
        return {"value": "" if value is None else value_text(value, display=display),
                "start": str(begin), "end": str(end),
                "frequency": str(frequency), "maximum": str(maximum),
                "leaders": ", ".join(value_text(item, display=display) for item in leaders),
                "size": str(len(ordered)), "group": str(group), "groups": str(len(groups)),
                "countedIndex": "" if counted_at is None else str(counted_at),
                "originIndex": "" if counted_at is None else str(source_indices[counted_at]),
                "processedCount": str(end), "remainingCount": str(len(ordered) - end),
                "previousMaximum": ("" if previous_maximum is None
                                    else str(previous_maximum))}

    def render(template, values):
        return re.sub(r"\{([^{}]+)\}", lambda match: values[match[1]], template)

    def frame(phase, values):
        nonlocal total
        total += len(render(spec["explanations"][phase], values).encode("utf-8"))
        total += len(output.encode("utf-8"))
        _require(total <= MAX_TRACE_BYTES,
                 "La traza de simulación supera 8 MiB de explicaciones y salida acumulada")

    if not ordered:
        frame("empty", context(0, 0, 0, 0, display=True))
        return total
    frame("initial", context(0, 0, 0, 0, display=True))
    for group, (begin, end, value) in enumerate(groups, 1):
        frame("start", context(begin, begin + 1, group, 1, value,
                               display=True, counted_at=begin))
        for at in range(begin + 2, end + 1):
            frame("advance", context(begin, at, group, at - begin, value,
                                     display=True, counted_at=at - 1))
        frequency = end - begin
        previous_maximum = maximum
        if frequency > maximum:
            maximum, leaders, phase = frequency, [value], "newMaximum"
        elif frequency == maximum:
            leaders.append(value)
            phase = "tie"
        else:
            phase = "belowMaximum"
        current = context(begin, end, group, frequency, value,
                          previous_maximum=previous_maximum)
        output += render(spec["outputTemplate"], current) + "\n"
        frame(phase, context(begin, end, group, frequency, value,
                             display=True, previous_maximum=previous_maximum))
    frame("done", context(len(ordered), len(ordered), len(groups), 0, display=True))
    return total


def validate_contract(block):
    """Validate the complete v1 block without changing any authored value."""
    _require(type(block) is dict and block.get("type") == "simulation",
             "Se esperaba un bloque simulation")
    _require(set(block) <= FIELDS and {"type", "title", "description", "simulation"} <= set(block),
             "simulation contiene campos desconocidos o faltan campos obligatorios")
    if "id" in block:
        _text(block["id"], "simulation.id", 128)
    _text(block["title"], "simulation.title", 120)
    _text(block["description"], "simulation.description", 4000)
    spec = block["simulation"]
    _require(type(spec) is dict and SPEC_FIELDS - {"scenarios"} <= set(spec) <= SPEC_FIELDS,
             "simulation requiere su configuración completa sin campos desconocidos")
    _require(_integer(spec["version"], 1, 1), "simulation.version debe ser 1")
    _require(spec["model"] == "arrayRunsV1", "Modelo de simulación no admitido")
    _require(type(spec["view"]) is str and spec["view"] in VIEWS,
             "Vista de simulación no admitida")
    _require(type(spec["valueType"]) is str and spec["valueType"] in {"integer", "text"},
             "simulation.valueType debe ser integer o text")
    _require(type(spec["sort"]) is bool and type(spec["editable"]) is bool,
             "simulation.sort y simulation.editable deben ser booleanos")
    labels = spec["labels"]
    _require(type(labels) is dict and set(labels) == LABEL_FIELDS,
             "simulation.labels requiere exactamente las siete etiquetas declaradas")
    for name, value in labels.items():
        _text(value, f"simulation.labels.{name}", 80)
    explanations = spec["explanations"]
    _require(type(explanations) is dict and set(explanations) == EXPLANATION_FIELDS,
             "simulation.explanations requiere exactamente las ocho fases declaradas")
    for name, value in explanations.items():
        validate_template(value, f"simulation.explanations.{name}", 2000)
    validate_template(spec["outputTemplate"], "simulation.outputTemplate", 256,
                      counting_explanation=False)
    _values(spec["values"], spec, "simulation.values")
    scenarios = spec.get("scenarios", [])
    _require(type(scenarios) is list and len(scenarios) <= MAX_SCENARIOS,
             f"simulation.scenarios requiere entre 0 y {MAX_SCENARIOS} escenarios")
    seen = set()
    for index, scenario in enumerate(scenarios):
        _require(type(scenario) is dict and set(scenario) == {"id", "label", "values"},
                 "Cada escenario requiere únicamente id, label y values")
        identity = scenario["id"]
        _require(type(identity) is str and _SCENARIO_ID.fullmatch(identity) is not None
                 and identity not in seen,
                 "Los escenarios requieren identificadores ASCII válidos y únicos de hasta 64 caracteres")
        seen.add(identity)
        _text(scenario["label"], f"simulation.scenarios[{index}].label", 80)
        _values(scenario["values"], spec, f"simulation.scenarios[{index}].values")
    _trace_text_bytes(spec, spec["values"])
    for scenario in scenarios:
        _trace_text_bytes(spec, scenario["values"])
    return spec


def validate_document(document):
    """Inspect active blocks and archived reading presentations alike."""
    def validate_block(block):
        spec = validate_contract(block)
        if "stateLayout" not in document:
            return
        # The existing Windows state-layout adapter accepts only integer JSON
        # tokens. Keep that document requirement without narrowing the isolated
        # simulation adapter or changing historical fingerprint normalization.
        _require(type(spec["version"]) is int,
                 "Una simulación con stateLayout requiere version como entero JSON sin punto ni exponente")
        if spec["valueType"] == "integer":
            sources = [spec["values"]] + [item["values"] for item in spec.get("scenarios", [])]
            _require(all(type(value) is int for values in sources for value in values),
                     "Una simulación con stateLayout requiere valores enteros JSON sin punto ni exponente")

    def pages(values):
        if type(values) is list:
            for page in values:
                if type(page) is dict and type(page.get("blocks")) is list:
                    for block in page["blocks"]:
                        if type(block) is dict and block.get("type") == "simulation":
                            validate_block(block)

    pages(document.get("pages"))
    meta = document.get("stateLayout", {})
    if type(meta) is not dict:
        return
    retired = meta.get("retired", [])
    if type(retired) is list:
        for item in retired:
            block = item.get("block") if type(item) is dict else None
            if type(block) is dict and block.get("type") == "simulation":
                validate_block(block)
    history = meta.get("retirementHistory", [])
    if type(history) is list:
        for snapshot in history:
            if type(snapshot) is dict:
                pages(snapshot.get("pages"))
