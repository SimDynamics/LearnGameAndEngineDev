"""Author and authenticate a new Windows course edition without deleting old assets.

An edition uses new curricular unit/lesson IDs. Existing preference keys,
recordings and historical content are never rewritten by these helpers.
"""
from __future__ import annotations

from copy import deepcopy
from importlib import util
import json
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import subprocess

CONTENT_DIRECTORY = "app/src/main/assets/content"
FIELD = "courseEdition"
FIELDS = {"version", "courseId", "locale", "id", "generation"}
LOCALES = frozenset({"es", "en", "fr"})


class CourseEditionError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise CourseEditionError(message)


def _safe(value, limit):
    return type(value) is str and re.fullmatch(r"[A-Za-z0-9_-]{1," + str(limit) + r"}", value) is not None


def _slug(value):
    return (_safe(value, 128) and "__" not in value
            and value[0].isascii() and value[0].isalnum()
            and value[-1].isascii() and value[-1].isalnum())


def descriptor(document):
    """Return a validated descriptor, or None for a historical document."""
    require(type(document) is dict, "A curricular document must be an object")
    if FIELD not in document:
        return None
    value = document[FIELD]
    require(type(value) is dict and set(value) == FIELDS, "Invalid courseEdition fields")
    require(type(value["version"]) is int and value["version"] == 1, "Unsupported courseEdition version")
    require(_safe(value["courseId"], 256) and "__edition_" not in value["courseId"],
            "Invalid courseEdition courseId")
    require(value["locale"] in LOCALES if type(value["locale"]) is str else False,
            "Invalid courseEdition locale")
    require(_safe(value["id"], 64) and value["id"] != "legacy", "Invalid courseEdition id")
    require(type(value["generation"]) is int and 1 <= value["generation"] <= 1_000_000,
            "Invalid courseEdition generation")
    return value


def unit_prefix(value):
    value = descriptor({FIELD: value})
    return f'{value["courseId"]}__edition_{value["locale"]}_{value["generation"]}__'


def unit_slug(identity, value):
    prefix = unit_prefix(value)
    require(type(identity) is str and len(identity) <= 256 and identity.startswith(prefix),
            "An edition unit must use its new course/locale/generation namespace")
    slug = identity[len(prefix):]
    require(_slug(slug), "Invalid edition unit slug")
    return slug


def lesson_parts(identity, value):
    prefix = unit_prefix(value)
    require(type(identity) is str and len(identity) <= 256 and identity.startswith(prefix),
            "An edition lesson must use its new course/locale/generation namespace")
    parts = identity[len(prefix):].split("__")
    require(len(parts) == 2 and all(_slug(part) for part in parts), "Invalid edition lesson slugs")
    return prefix + parts[0], parts[0], parts[1]


def validate_lesson(document, *, require_layout=False):
    """Validate edition identity independently of a file's publication context."""
    value = descriptor(document)
    if value is None:
        require("__edition_" not in str(document.get("id", "")),
                "The edition namespace requires courseEdition")
        return None
    lesson_parts(document.get("id"), value)
    meta = document.get("stateLayout")
    if require_layout:
        require(type(meta) is dict and meta.get("version") == 2,
                "An active edition lesson requires a complete stateLayout v2")
    if type(meta) is dict and meta.get("version") == 2:
        require(meta.get("locale") == value["locale"]
                and type(meta.get("family")) is dict and set(meta["family"]) == {value["locale"]},
                "An edition map must use only its explicitly authored locale")
    return value


def validate_transition(previous_course, current_course):
    """Authenticate a monotonic edition boundary; never reset an existing one."""
    before, after = descriptor(previous_course), descriptor(current_course)
    if before is None and after is None:
        return False
    require(after is not None, "A published courseEdition cannot be removed")
    require(current_course.get("id") == after["courseId"], "The edition does not belong to its course index")
    require(previous_course.get("id") == current_course.get("id"), "A course edition cannot rename its course")
    if before == after:
        return False
    if before is None:
        require(after["generation"] == 1, "The first course edition must have generation 1")
    else:
        require((before["courseId"], before["locale"]) == (after["courseId"], after["locale"]),
                "A course edition cannot change its course or source locale")
        require(after["generation"] == before["generation"] + 1,
                "A new course edition must advance exactly one generation")
        require(after["id"] != before["id"], "A new course edition needs a different descriptive id")
    return True


def _entries(document, field):
    entries = document.get(field)
    require(type(entries) is list and bool(entries), f"An edition index needs nonempty {field}")
    require(all(type(item) is dict and type(item.get("id")) is str for item in entries),
            f"Malformed edition {field}")
    identities = [item["id"] for item in entries]
    require(len(set(identities)) == len(identities), f"Repeated edition {field}")
    return identities


def _active_index_entries(document, field):
    """Authenticate the visible title and ordinal as well as the owner ID.

    Windows uses these exact entries to present the lesson route. A valid
    target identifier must not lend its provenance to a misleading title
    or to a duplicate/out-of-order position. Retired historical indexes
    remain unchanged and are handled by their legacy validator.
    """
    _entries(document, field)
    entries = document[field]
    for ordinal, entry in enumerate(entries, 1):
        require(set(entry) == {"id", "number", "title"},
                f"Una entrada activa de {field} requiere id, number y title")
        require(type(entry["number"]) is int and entry["number"] == ordinal,
                f"Numeración incorrecta en índice activo de {field}")
        require(type(entry["title"]) is str and bool(entry["title"].strip()),
                f"Título vacío en índice activo de {field}")
    return entries


def validate_active_course(course, units, lessons):
    """Check active references; inactive historical resources stay outside this walk."""
    value = descriptor(course)
    if value is None:
        for unit_id in _entries(course, "units"):
            require("__edition_" not in unit_id, "A legacy course cannot activate an edition unit")
            unit = units[unit_id]
            require(descriptor(unit) is None, "A legacy course cannot activate an edition unit")
            for lesson_id in _entries(unit, "lessons"):
                require("__edition_" not in lesson_id and descriptor(lessons[lesson_id]) is None,
                        "A legacy course cannot activate an edition lesson")
        return
    require(course.get("id") == value["courseId"], "The edition belongs to a different course")
    require(type(course.get("title")) is str and bool(course["title"].strip()),
            "El índice de curso activo necesita un título")
    for unit_entry in _active_index_entries(course, "units"):
        unit_id = unit_entry["id"]
        unit_slug(unit_id, value)
        require(unit_id in units, "An edition index references a missing unit")
        unit = units[unit_id]
        require(unit.get("courseId") == value["courseId"] and unit.get("unitId") == unit_id
                and descriptor(unit) == value, "An active unit does not match its courseEdition")
        require(type(unit.get("title")) is str and
                unit_entry["title"] == unit["title"],
                "El título de unidad difiere del índice de curso activo")
        for lesson_entry in _active_index_entries(unit, "lessons"):
            lesson_id = lesson_entry["id"]
            expected_unit, _, _ = lesson_parts(lesson_id, value)
            require(expected_unit == unit_id, "An edition index references a lesson of another unit")
            require(lesson_id in lessons, "An edition index references a missing lesson")
            lesson = lessons[lesson_id]
            require(lesson.get("id") == lesson_id and descriptor(lesson) == value,
                    "An active lesson does not match its courseEdition")
            require(type(lesson.get("title")) is str and
                    lesson_entry["title"] == lesson["title"],
                    "El título de lección difiere del índice de unidad activo")
            validate_lesson(lesson, require_layout=True)


def _state_module():
    # Dynamic import preserves the project's existing importlib-based callers.
    spec = util.spec_from_file_location("lgaed_edition_state_layout", Path(__file__).with_name("stable_state_layout.py"))
    module = util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def literal_layout(document, *, pages, state_fingerprint):
    """Describe only the newly delivered source, without a fictional older origin."""
    origin, current = [], []
    for p, page in enumerate(pages(document)):
        page_id = f"page_{p:04d}"
        blocks = [{"id": f"block_{p:04d}_{b:04d}", "sha256": state_fingerprint(block)}
                  for b, block in enumerate(page.get("blocks", []))]
        origin.append({"id": page_id, "blocks": blocks})
        current.append({"id": page_id, "blocks": [item["id"] for item in blocks]})
    return {"origin": origin, "current": current}


def prepare_edition_lesson(document, value):
    """Return a fresh, validated v2 lesson. Never edit an old map or an input."""
    value = deepcopy(descriptor({FIELD: value}))
    require(type(document) is dict and "stateLayout" not in document,
            "Prepare an authored new edition source without an earlier stateLayout")
    require(FIELD not in document or descriptor(document) == value,
            "The source belongs to a different courseEdition")
    result = deepcopy(document)
    result[FIELD] = value
    validate_lesson(result)
    state = _state_module()
    literal = literal_layout(result, pages=state._pages, state_fingerprint=state.state_fingerprint)
    source = {key: val for key, val in result.items() if key not in state.TEXT_PRESENTATION_FIELDS}
    result["stateLayout"] = {
        "version": 2, "locale": value["locale"],
        "origin": literal["origin"], "current": literal["current"],
        "family": {value["locale"]: {"sha256": state.fingerprint(source),
                                    "origin": deepcopy(literal["origin"])}},
        "additions": [], "editorial": [],
    }
    state.validate(result)
    validate_lesson(result, require_layout=True)
    return result


def _parse(text):
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, "Duplicate JSON key")
            result[key] = value
        return result
    return json.loads(text, object_pairs_hook=pairs,
                      parse_constant=lambda _: (_ for _ in ()).throw(CourseEditionError("Non-finite number")))


def _load(path):
    require(not path.is_symlink(), "Edition resources must not be symbolic links")
    return _parse(path.read_text(encoding="utf-8-sig"))


def _git_bytes(repository, commit, relative):
    result = subprocess.run(["git", "show", commit + ":" + relative, "--"],
                            cwd=repository, capture_output=True)
    require(result.returncode == 0, "Cannot read the published edition resource: " + relative)
    return result.stdout


def _paths_at(repository, commit):
    result = subprocess.run(["git", "ls-tree", "-r", "--name-only", "-z", commit,
                             "--", CONTENT_DIRECTORY], cwd=repository,
                            capture_output=True, text=True, encoding="utf-8")
    require(result.returncode == 0, "Cannot list the published edition resources")
    return {item for item in result.stdout.split("\0") if item}


def _course_path_parts(relative):
    parts = PurePosixPath(relative).relative_to(CONTENT_DIRECTORY).parts
    require(len(parts) == 4 and parts[0] in LOCALES
            and parts[3] == f"course_{parts[2]}.json", "Invalid edition course index path")
    return parts[0], parts[1], parts[2]


def _edition_file(repository, relative, role):
    """Require a real edition asset, not a link in any ancestor directory.

    Both active and preserved historical resources must remain physical files
    under the same repository tree. A symlink or Windows junction through an
    ancestor is not equivalent to retaining that published resource.
    """
    requested = str(relative)
    relative = PurePosixPath(requested)
    spelling = relative.as_posix()
    windows = PureWindowsPath(spelling)
    # Inspect the unnormalized Git spelling. Collapsing '.' or duplicate
    # separators could make a different historical pathname look authentic.
    # Windows also interprets colons as alternate data streams and reserves
    # device names (even when followed by an extension). Never follow them.
    def invalid_windows_component(part):
        stem = part.split(".", 1)[0].rstrip(" ").upper()
        reserved = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"}
        device = (len(stem) == 4 and stem[:3] in {"COM", "LPT"}
                  and stem[3] in "123456789¹²³")
        return (part[-1] in ". " or stem in reserved or device
                or any(ord(char) < 32 or char in '<>:"|?*' for char in part))

    require(bool(relative.parts) and requested == spelling
            and not relative.is_absolute() and ".." not in relative.parts
            and "\\" not in requested and not windows.drive and not windows.root
            and not any(invalid_windows_component(part) for part in relative.parts),
            "Unsafe edition asset path")
    path = repository
    for part in relative.parts:
        path = path / part
        junction = getattr(path, "is_junction", None)
        require(not path.is_symlink() and not (junction is not None and junction()),
                f"An edition {role} cannot use a symbolic alias or junction")
    require(path.is_file(), f"Missing edition {role}: {relative.as_posix()}")
    return path


def _active_documents(repository, course_relative, course):
    folder = PurePosixPath(course_relative).parent
    course_id = course.get("id")
    require(_safe(course_id, 256), "Invalid course index identity")
    _edition_file(repository, course_relative, "course index")
    units, lessons, paths = {}, {}, {}
    for unit_id in _entries(course, "units"):
        prefix = course_id + "__"
        require(unit_id.startswith(prefix), "A unit index belongs to another course")
        unit_folder = unit_id[len(prefix):]
        require(_safe(unit_folder, 256), "Unsafe unit folder")
        relative = (folder / unit_folder / f"unit_{unit_folder}.json").as_posix()
        path = _edition_file(repository, relative, "unit")
        require(path.resolve().is_relative_to((repository / folder).resolve()), "Unit escapes its course directory")
        unit = _load(path)
        units[unit_id], paths[unit_id] = unit, relative
        for lesson_id in _entries(unit, "lessons"):
            require(lesson_id.startswith(unit_id + "__"), "A lesson index belongs to another unit")
            slug = lesson_id[len(unit_id) + 2:]
            require(_safe(slug, 256), "Unsafe lesson filename")
            relative = (folder / unit_folder / f"lesson_{slug}.json").as_posix()
            path = _edition_file(repository, relative, "lesson")
            require(path.resolve().is_relative_to((repository / folder).resolve()), "Lesson escapes its course directory")
            lesson = _load(path)
            require(lesson_id not in lessons, "An edition activates a lesson more than once")
            lessons[lesson_id], paths[lesson_id] = lesson, relative
    validate_active_course(course, units, lessons)
    return units, lessons, paths


def _reject_windows_case_aliases(paths):
    """Protect every directory component, not only equal final file names.

    NTFS can combine Git paths like Unit/old.json and unit/new.json in one
    directory. Full-file casefold comparisons miss that collision, and a
    later author could incorrectly treat two unit directories as distinct.
    """
    exact_by_folded = {}
    for spelling in sorted(paths):
        parts = PurePosixPath(spelling).parts
        for length in range(1, len(parts) + 1):
            prefix = "/".join(parts[:length])
            previous = exact_by_folded.setdefault(prefix.casefold(), prefix)
            require(previous == prefix,
                    "Windows case-insensitive edition path collision: "
                    + previous + " / " + prefix)


def check_repository(repository, commit, *, documents=None):
    """Publication gate for index boundaries; ordinary state guards still run separately."""
    repository = Path(repository).resolve()
    content = repository / CONTENT_DIRECTORY
    previous_paths = _paths_at(repository, commit)
    current_courses = {path.relative_to(repository).as_posix(): _load(path)
                       for path in content.glob("*/*/*/course_*.json")}
    previous_courses = {}
    for relative in sorted(previous_paths):
        if PurePosixPath(relative).name.startswith("course_") and relative.endswith(".json"):
            previous_courses[relative] = _parse(_git_bytes(repository, commit, relative).decode("utf-8-sig"))
    documents = ({path.relative_to(repository).as_posix(): _load(path)
                  for path in content.rglob("lesson_*.json")} if documents is None else documents)
    current_units = {path.relative_to(repository).as_posix(): _load(path)
                     for path in content.rglob("unit_*.json")}
    declared = any(FIELD in document for document in [*current_courses.values(), *previous_courses.values()])
    if not declared:
        require(not any(FIELD in document or "__edition_" in str(document.get("id", ""))
                        for document in documents.values())
                and not any(FIELD in document or "__edition_" in str(document.get("unitId", ""))
                            for document in current_units.values()),
                "An edition resource requires its explicitly published course index")
        return {"course_edition_boundaries": [], "active_edition_resources": []}
    # Check all physical content path spellings before accepting an edition.
    # A new case-variant sibling can be unindexed (e.g. QA_EDITION/notes.json)
    # or even empty; neither its name nor its parent would appear among the
    # course_*.json indexes or a scan limited to the active course. A Windows
    # checkout aliases that folder to an existing published course directory.
    # Include history as well as present files and directories, so deleting
    # one spelling at the same time cannot mask an unsafe transition.
    content_prefix = CONTENT_DIRECTORY.casefold() + "/"
    historical_content = [
        path for path in previous_paths
        if path.casefold().startswith(content_prefix)
    ]
    physical_content = [
        path.relative_to(repository).as_posix()
        for path in content.rglob("*")
        if path.is_file() or path.is_dir() or path.is_symlink()
    ]
    _reject_windows_case_aliases([*historical_content, *physical_content])
    boundaries = []
    active_paths = set()
    for relative in sorted(current_courses.keys() | previous_courses.keys()):
        current, previous = current_courses.get(relative), previous_courses.get(relative)
        before = descriptor(previous) if previous is not None else None
        after = descriptor(current) if current is not None else None
        if before is None and after is None:
            continue
        require(current is not None and previous is not None, "An edition needs its existing published course index")
        locale, _, course_id = _course_path_parts(relative)
        require(after is not None and (after["locale"], after["courseId"]) == (locale, course_id),
                "The courseEdition must match its physical course and locale")
        # Check unit paths declared in the course index before loading any
        # file. A case-insensitive checkout may already have overwritten the
        # first unit with a differently cased name.
        folder = PurePosixPath(relative).parent
        prefix = folder.as_posix() + "/"
        historical = [p for p in previous_paths
                      if p.casefold().startswith(prefix.casefold())]
        declared_units = []
        for unit_id in _entries(current, "units"):
            require(unit_id.startswith(course_id + "__"),
                    "A unit index belongs to another course")
            slug = unit_id[len(course_id) + 2:]
            require(_safe(slug, 256), "Unsafe unit folder")
            declared_units.append((folder / slug / f"unit_{slug}.json").as_posix())
        _reject_windows_case_aliases([*historical, *declared_units])

        advanced = validate_transition(previous, current)
        units, lessons, paths = _active_documents(repository, relative, current)
        # An unindexed Lesson_x.json may alias an active lesson_x.json.
        # Scan the actual course folder as well as the indexed and archived
        # names, without reading or modifying the unindexed file.
        physical_course = repository / folder
        physical_files = [item.relative_to(repository).as_posix()
                          for item in physical_course.rglob("*") if item.is_file()]
        _reject_windows_case_aliases([*historical, *paths.values(), *physical_files])
        if advanced:
            for path in paths.values():
                require(path not in previous_paths, "A new edition must use genuinely new unit and lesson resources")
            boundaries.append(relative)
        else:
            # Compatible updates can add/reorder content. Retiring a published
            # unit or lesson needs the explicit next edition boundary.
            for unit_id in _entries(previous, "units"):
                require(unit_id in units, "A published edition unit cannot leave its active index")
                previous_unit = _parse(_git_bytes(repository, commit, paths[unit_id]).decode("utf-8-sig"))
                require(set(_entries(previous_unit, "lessons")) <= set(_entries(units[unit_id], "lessons")),
                        "A published edition lesson cannot leave its active index")
        for path in sorted(previous_paths):
            if path.startswith(prefix) and path != relative and (advanced or path not in paths.values()):
                # The bytes alone do not prove preservation if an ancestor is
                # now a symlink/junction. Apply the same physical-path check
                # used for active edition units and lessons.
                existing = repository / path
                require(existing.is_file(),
                        "A course edition must retain its previous resources intact: " + path)
                retained = _edition_file(repository, path, "historical resource")
                require(retained.read_bytes() == _git_bytes(repository, commit, path),
                        "A course edition must retain its previous resources intact: " + path)
        active_paths.update(paths.values())
    # Any new descriptor needs an index, including metadata omitted from an active
    # course. Existing inactive editions remain readable through their old assets.
    for relative, document in sorted({**documents, **current_units}.items()):
        path = repository / relative
        is_lesson = path.name.startswith("lesson_")
        value = validate_lesson(document) if is_lesson else descriptor(document)
        identity = document.get("id" if is_lesson else "unitId", "")
        if value is None:
            require("__edition_" not in str(identity), "The edition namespace requires courseEdition")
            continue
        if not is_lesson:
            unit_slug(identity, value)
        if relative not in active_paths:
            require(relative in previous_paths,
                    "An edition resource must be active or an unchanged historical resource: " + relative)
            retained = _edition_file(repository, relative, "inactive historical resource")
            require(retained.read_bytes() == _git_bytes(repository, commit, relative),
                    "An edition resource must be active or an unchanged historical resource: " + relative)
    return {"course_edition_boundaries": boundaries, "active_edition_resources": sorted(active_paths)}


def prepare_from_repository(source, base_ref, *, locale=None):
    """Prepare one new asset only after checking its explicit indexed edition."""
    # Keep the original requested path until every ancestor has been checked.
    # Resolving it first would hide a symlink/junction and could prepare a
    # different physical lesson than the one Work selected.
    requested = Path(source).absolute()
    result = subprocess.run(["git", "rev-parse", "--show-toplevel"], cwd=requested.parent,
                            capture_output=True, text=True, encoding="utf-8")
    require(result.returncode == 0, "Prepare requires the existing project checkout")
    repository = Path(result.stdout.strip()).resolve()
    try:
        relative = requested.relative_to(repository).as_posix()
    except ValueError as exc:
        raise CourseEditionError("Authoring source is outside the project checkout") from exc
    source = _edition_file(repository, relative, "authoring lesson")
    state = _state_module()
    commit = state.resolve_base_commit(repository, base_ref)
    parts = PurePosixPath(relative).relative_to(CONTENT_DIRECTORY).parts
    require(len(parts) == 5 and parts[0] in LOCALES and parts[-1].startswith("lesson_")
            and parts[-1].endswith(".json"), "Prepare requires an edition lesson asset")
    require(locale is None or locale == parts[0], "--locale must match the edition resource")
    previous_paths = _paths_at(repository, commit)
    require(relative not in previous_paths, "A published edition source must use ordinary authenticated revisions")
    course_relative = f"{CONTENT_DIRECTORY}/{parts[0]}/{parts[1]}/{parts[2]}/course_{parts[2]}.json"
    course = _load(_edition_file(repository, course_relative, "authoring course index"))
    value = descriptor(course)
    require(value is not None, "The course index must declare courseEdition before preparation")
    document = _load(source)
    require("stateLayout" not in document, "A new edition source must not recycle an earlier stateLayout")
    prepared = prepare_edition_lesson(document, value)
    # Preparation accepts a partially authored tree. Authentication of this one
    # new source does not claim that the remaining edition is ready to publish.
    require(course_relative in previous_paths, "An edition needs its existing published course index")
    previous_course = _parse(_git_bytes(repository, commit, course_relative).decode("utf-8-sig"))
    validate_transition(previous_course, course)
    require((value["locale"], value["courseId"]) == (parts[0], parts[2]),
            "The courseEdition must match the authoring source path")
    unit_id, _, slug = lesson_parts(prepared["id"], value)
    unit_folder = unit_id[len(value["courseId"]) + 2:]
    require(parts[3] == unit_folder and parts[4] == f"lesson_{slug}.json",
            "The authoring lesson identity does not match its new resource path")
    require(unit_id in _entries(course, "units"), "The authoring unit must be active in the edition index")
    unit_relative = (PurePosixPath(relative).parent / f"unit_{unit_folder}.json").as_posix()
    unit_path = _edition_file(repository, unit_relative, "authoring unit index")
    unit = _load(unit_path)
    require(unit.get("courseId") == value["courseId"] and unit.get("unitId") == unit_id
            and descriptor(unit) == value and prepared["id"] in _entries(unit, "lessons"),
            "The authoring lesson must belong to its explicitly indexed edition unit")
    for old_relative in sorted(previous_paths):
        if PurePosixPath(old_relative).name.startswith("lesson_") and old_relative.endswith(".json"):
            old = _parse(_git_bytes(repository, commit, old_relative).decode("utf-8-sig"))
            require(old.get("id") != prepared["id"], "A new edition cannot reuse a published lesson identity")
    return prepared
