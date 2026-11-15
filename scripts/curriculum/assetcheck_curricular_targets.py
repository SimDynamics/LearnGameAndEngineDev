"""Authenticated owners of the four AssetCheck reference contracts under QA.

Lessons may contain other valid editors with different input protocols. The
independent oracles and C++ reference compiler must inspect the declared owner,
not whichever editor happens to appear first or to share a function name.
"""

from __future__ import annotations

if __package__:
    from . import stable_state_layout as state
else:
    import stable_state_layout as state

EDITOR_OWNERS = {
    "lesson_leer_y_validar_un_registro.json": "block_0005_0002",
    "lesson_procesar_un_lote_y_detectar_duplicados.json": "block_0005_0002",
    "lesson_leer_manifiesto_y_publicar_informe.json": "block_0006_0002",
    "lesson_disenar_probar_y_presentar_variante_propia.json": "block_0006_0003",
}


def blocks_by_identity(lesson: dict, *, filename: str) -> dict[str, dict]:
    """Resolve current owners only after validating their published contracts."""
    if type(lesson) is not dict:
        raise ValueError(f"{filename}: the lesson must be an object")
    pages = lesson.get("pages")
    layout = lesson.get("stateLayout")
    current = layout.get("current") if type(layout) is dict else None
    if type(pages) is not list or type(current) is not list or len(pages) != len(current):
        raise ValueError(f"{filename}: stateLayout.current must align with every page")
    for page_number, (page, record) in enumerate(zip(pages, current, strict=True), start=1):
        if (type(page) is not dict or type(page.get("blocks", [])) is not list or
                type(record) is not dict or type(record.get("blocks")) is not list):
            raise ValueError(f"{filename}: stateLayout.current has a malformed page {page_number}")
        if len(record["blocks"]) != len(page.get("blocks", [])):
            raise ValueError(
                f"{filename}: stateLayout.current must align with every block of page {page_number}")

    # Syntax and uniqueness alone cannot prove that an owner still names its
    # original activity. Reuse the shared state/relationship validation before
    # constructing a dictionary, including its immutable payload fingerprints.
    try:
        state.validate(lesson)
    except (ValueError, KeyError, TypeError) as error:
        raise ValueError(f"{filename}: invalid stable lesson contract: {error}") from error
    return {
        ident: block
        for page, record in zip(pages, current, strict=True)
        for ident, block in zip(record["blocks"], page.get("blocks", []), strict=True)
    }


def declared_editor(lesson: dict, *, filename: str) -> tuple[str, dict, dict[str, dict]]:
    """Return the declared active editor and the authenticated block map."""
    try:
        owner = EDITOR_OWNERS[filename]
    except KeyError as error:
        raise ValueError(f"{filename}: no declared curricular editor") from error
    blocks = blocks_by_identity(lesson, filename=filename)
    if owner not in blocks:
        raise ValueError(f"{filename}: missing declared C++ editor {owner}")
    editor = blocks[owner]
    if editor["type"] != "cpp_compiler":
        raise ValueError(f"{filename}: declared owner {owner} is not a C++ editor")
    return owner, editor, blocks
