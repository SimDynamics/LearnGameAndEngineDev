"""Conservative, author-declared C++ identifier changes in existing lesson sources.

This is a lexical guard, NOT a compiler, name-resolution engine or permission to
update published state. Scope/binding and program equivalence still require the
author's C++ build, output checks and an authenticated state-layout transition.
The source-format guard remains independent and continues rejecting renames.
"""
from __future__ import annotations

from collections import Counter
import math
import re

import code_format_compat as source_format


class IdentifierRenameError(ValueError):
    """A proposed rename changes material outside its explicit source contract."""


_IDENTIFIER = re.compile(r"[A-Za-z][A-Za-z0-9_]*\Z", flags=re.ASCII)
_CPP_KEYWORDS = frozenset("""
alignas alignof and and_eq asm auto bitand bitor bool break case catch char
char8_t char16_t char32_t class compl concept const consteval constexpr
constinit const_cast continue co_await co_return co_yield decltype default
delete do double dynamic_cast else enum explicit export extern false float for
friend goto if inline int long mutable namespace new noexcept not not_eq
nullptr operator or or_eq private protected public register reinterpret_cast
requires return short signed sizeof static static_assert static_cast struct
switch template this thread_local throw true try typedef typeid typename union
unsigned using virtual void volatile wchar_t while xor xor_eq
import module final override
""".split())


def _declared_name(name: object) -> bool:
    return (type(name) is str and _IDENTIFIER.fullmatch(name) is not None
            and name not in _CPP_KEYWORDS and "__" not in name)


_SPELLING_OBSERVERS = frozenset({
    "__func__", "__FUNCTION__", "__PRETTY_FUNCTION__", "__FUNCSIG__",
    "__FUNCDNAME__", "typeid",
    # C++20 source_location::current().function_name() observes a
    # declaration's spelling even when the revision changes only id tokens.
    "source_location", "__builtin_FUNCTION",
})


def _cpp_spelling_sensitive_tokens(tokens: tuple[str, ...]) -> bool:
    """Reject observable function/type spelling and macro stringize/paste.

    A name-only token diff is insufficient if the program can print a renamed
    function's name, or a macro can stringify/paste its argument into data.
    """
    for token in tokens:
        if token.startswith("id:") and token[3:] in _SPELLING_OBSERVERS:
            return True
        if token.startswith("directive:"):
            directive = token[len("directive:"):]
            if directive.startswith("#"):
                directive = directive[1:]
            elif directive.startswith("%:"):
                directive = directive[2:]
            match = re.match(r"\s*define\b", directive)
            if match and ("#" in directive[match.end():]
                          or "%:" in directive[match.end():]):
                return True
    return False


def same_json_value_and_types(previous: object, current: object) -> bool:
    """Compare a protected JSON contract without Python's bool/int coercion.

    Plain dict/list equality silently treats True == 1 and 1 == 1.0, even
    though these are different JSON payloads and runtime fingerprints. Only
    identical primitive types and recursively identical values are allowed.
    """
    def equal(before: object, after: object, depth: int) -> bool:
        if depth > 64 or type(before) is not type(after):
            return False
        if type(before) is dict:
            return (all(type(key) is str for key in before)
                    and before.keys() == after.keys()
                    and all(equal(before[key], after[key], depth + 1)
                            for key in before))
        if type(before) is list:
            return (len(before) == len(after)
                    and all(equal(a, b, depth + 1)
                            for a, b in zip(before, after)))
        if type(before) is float:
            # Python considers 0.0 == -0.0, but a sign-preserving JSON
            # representation (and a possible C++ floating-point contract)
            # must not change inside an identifier-only source revision.
            # NaN and infinity are not legal JSON numbers at all.
            return (math.isfinite(before) and math.isfinite(after)
                    and before == after
                    and (before != 0.0
                         or math.copysign(1.0, before) ==
                            math.copysign(1.0, after)))
        if before is None or type(before) in (str, bool, int):
            return before == after
        return False

    return equal(previous, current, 0)


def _exact_identifier_substitution(
    source: str, tokens: tuple[str, ...], names: dict[str, str],
) -> str:
    """Reassemble the unchanged physical source, replacing only id tokens.

    The shared C++ lexer owns the token boundaries. Recover each complete
    token from the exact source, retaining all spaces, comments, literals,
    line endings and directives verbatim. Token-equivalence alone would allow
    a hidden simultaneous reformat, which belongs to another signed archive.
    """
    chunks: list[str] = []
    position = 0
    for token in tokens:
        kind, literal = token.split(":", 1)
        start = source.find(literal, position)
        if start < position or any(c not in source_format._WS
                                   for c in source[position:start]):
            raise IdentifierRenameError("The C++ token cannot be located in its source")
        chunks.append(source[position:start])
        chunks.append(names.get(literal, literal) if kind == "id" else literal)
        position = start + len(literal)
    chunks.append(source[position:])
    if any(c not in source_format._WS for c in source[position:]):
        raise IdentifierRenameError("Unclassified trailing C++ token data")
    return "".join(chunks)


def require_declared_cpp_renames(
    original: dict, revised: dict,
    renames: dict[str, dict[str, str]],
    occurrences: dict[str, dict[str, int]],
) -> dict[str, dict[str, int]]:
    """Verify only declared old->new id tokens change within specified code slots.

    Each source slot must declare *every* occurrence of each old identifier and
    its exact positive count. All remaining C++ tokens, including comments,
    literal strings, preprocessor directives, operators and numbers, remain
    exactly the same. A destination cannot already be another identifier in
    that source slot. Code slots and all non-code payload fields are immutable.

    A name used by a macro is refused because a preprocessor replacement
    cannot be proved by lexical token comparison. Qualified names, shadowing,
    external linkage, side effects, output semantics and compiler correctness
    still require independent authored/compiled verification.
    """
    if type(renames) is not dict or not renames or type(occurrences) is not dict:
        raise IdentifierRenameError("A rename needs declared source slots and exact counts")
    if renames.keys() != occurrences.keys():
        raise IdentifierRenameError("Every renamed source slot needs its own counts")
    try:
        old_slots = source_format.code_slots(original)
        new_slots = source_format.code_slots(revised)
        if (original.get("type") != revised.get("type")
                or old_slots.keys() != new_slots.keys()
                or not same_json_value_and_types(
                    source_format.source_projection(revised, original), original)):
            raise IdentifierRenameError(
                "A rename cannot modify answers, metadata, file paths or other fields")
    except (ValueError, KeyError, TypeError, IndexError) as error:
        if isinstance(error, IdentifierRenameError):
            raise
        raise IdentifierRenameError(f"Invalid source owner: {error}") from error

    if not renames.keys() <= old_slots.keys():
        raise IdentifierRenameError("The rename declares an unknown code slot")

    # Cross-source protection: a macro defined in an unchanged header or
    # project file can capture a replacement in another code slot. Checking
    # only the edited slot would silently accept that different expansion.
    # Existing destination identifiers anywhere in the same authored block
    # are also ambiguous without binding/linkage analysis.
    if any(type(mapping) is not dict or not mapping or not all(
            _declared_name(name) and _declared_name(target)
            for name, target in mapping.items()) for mapping in renames.values()):
        raise IdentifierRenameError(
            "Each source slot needs valid declared C++ identifier names")
    protected_names = {
        name for mapping in renames.values() for name in mapping
    } | {
        name for mapping in renames.values() for name in mapping.values()
    }
    # The same old identifier may legitimately be renamed consistently in
    # several translation units (declaration, definition, and callers).
    # Conversely, mapping two DIFFERENT old names onto one new name silently
    # merges symbols; inconsistent renaming of one old name also lacks a
    # trustworthy binding contract. Do not forbid repeated identical pairs.
    original_to_new: dict[str, str] = {}
    new_to_original: dict[str, str] = {}
    for names in renames.values():
        for before, after in names.items():
            if before in original_to_new and original_to_new[before] != after:
                raise IdentifierRenameError(
                    "An old C++ identifier has inconsistent cross-file replacements")
            if after in new_to_original and new_to_original[after] != before:
                raise IdentifierRenameError(
                    "Distinct C++ identifiers reuse a destination across slots")
            original_to_new[before] = after
            new_to_original[after] = before
    destinations = set(new_to_original)
    for source_slot, (language, source) in old_slots.items():
        if language == "cmake":
            # An untouched CMake project can still inject a C++ macro through
            # target_compile_definitions/add_compile_definitions, configure
            # templates, flags or an indirect variable. Fail closed on a live
            # argument mentioning either renamed spelling; inert comments
            # are not executable macro definitions.
            try:
                cmake_tokens = source_format._cmake_tokens(source)
            except source_format.SourceFormatError as error:
                raise IdentifierRenameError(
                    f"Cannot verify CMake macro provenance in {source_slot}: {error}") from error
            if any(
                token.startswith(("word:", "quoted:", "bracket:"))
                and not token.startswith("bracket:#")
                and any(
                    re.search(r"(?<![A-Za-z0-9_])" + re.escape(name)
                              + r"(?![A-Za-z0-9_])", token)
                    for name in protected_names
                )
                for token in cmake_tokens
            ):
                raise IdentifierRenameError(
                    f"CMake project arguments mention a renamed C++ symbol: {source_slot}")
            continue
        if language != "cpp":
            continue
        try:
            tokens = source_format._cpp_tokens(source)
        except source_format.SourceFormatError as error:
            raise IdentifierRenameError(
                f"Cannot verify cross-file C++ macros in {source_slot}: {error}") from error
        if _cpp_spelling_sensitive_tokens(tokens):
            raise IdentifierRenameError(
                f"Source may observe renamed C++ symbol spelling: {source_slot}")
        for token in tokens:
            if token.startswith("directive:") and any(
                re.search(r"(?<![A-Za-z0-9_])" + re.escape(name)
                          + r"(?![A-Za-z0-9_])", token)
                for name in protected_names
            ):
                raise IdentifierRenameError(
                    f"Preprocessor directive refers to a renamed C++ identifier: {source_slot}")
            if not token.startswith("id:"):
                continue
            name = token[3:]
            if name in destinations:
                raise IdentifierRenameError(
                    f"A destination already exists in a published C++ source: {source_slot}")
            if source_slot not in renames and name in original_to_new:
                raise IdentifierRenameError(
                    f"An unchanged C++ source still refers to a renamed identifier: {source_slot}")

    confirmed: dict[str, dict[str, int]] = {}
    for slot, (language, before) in old_slots.items():
        current_language, after = new_slots[slot]
        if current_language != language:
            raise IdentifierRenameError(f"Source language changed: {slot}")
        if slot not in renames:
            if before != after:
                raise IdentifierRenameError(f"An undeclared code slot changed: {slot}")
            continue
        names, counts = renames[slot], occurrences[slot]
        if (language != "cpp" or type(names) is not dict or not names
                or type(counts) is not dict or names.keys() != counts.keys()):
            raise IdentifierRenameError(
                f"Only explicitly counted C++ source identifiers may change: {slot}")
        if (not all(_declared_name(name) and _declared_name(new)
                    and name != new for name, new in names.items())
                or len(set(names.values())) != len(names)):
            raise IdentifierRenameError(f"Invalid, reserved or colliding rename: {slot}")
        if before == after or before.count("\n") != after.count("\n"):
            raise IdentifierRenameError(
                f"Rename missing or changed the physical source lines: {slot}")
        if any(piece in before or piece in after for piece in ("\\\n", "\\\r\n")):
            raise IdentifierRenameError(f"Cannot rename a physically spliced C++ source: {slot}")
        try:
            old_tokens = source_format._cpp_tokens(before)
            new_tokens = source_format._cpp_tokens(after)
        except source_format.SourceFormatError as error:
            raise IdentifierRenameError(f"Invalid C++ tokenization in {slot}: {error}") from error
        if (source_format._cpp_position_sensitive_tokens(old_tokens)
                or source_format._cpp_position_sensitive_tokens(new_tokens)):
            raise IdentifierRenameError(
                f"A position-sensitive source cannot receive a lexical rename: {slot}")
        if ("op:\\" in old_tokens or "op:\\" in new_tokens
                or len(old_tokens) != len(new_tokens)):
            raise IdentifierRenameError(f"Unsafe C++ token boundaries: {slot}")
        old_identifiers = {t[3:] for t in old_tokens if t.startswith("id:")}
        if any(new in old_identifiers for new in names.values()):
            raise IdentifierRenameError(f"Rename destination collides with a source name: {slot}")
        for token in old_tokens:
            if token.startswith("directive:") and any(
                re.search(r"(?<![A-Za-z0-9_])" + re.escape(name)
                          + r"(?![A-Za-z0-9_])", token) for name in names
            ):
                raise IdentifierRenameError(f"Identifier is used by a directive: {slot}")
        changed: Counter[str] = Counter()
        for previous, proposed in zip(old_tokens, new_tokens):
            if previous == proposed:
                continue
            if (not previous.startswith("id:")
                    or previous[3:] not in names
                    or proposed != "id:" + names[previous[3:]]):
                raise IdentifierRenameError(
                    f"Nondeclared token, literal, comment or operator changed: {slot}")
            changed[previous[3:]] += 1
        expected = {}
        for name, number in counts.items():
            if type(number) is not int or number <= 0:
                raise IdentifierRenameError(f"Expected counts must be positive integers: {slot}")
            if sum(t == "id:" + name for t in old_tokens) != number:
                raise IdentifierRenameError(
                    f"Declared count excludes a source occurrence or shadowing: {slot}/{name}")
            expected[name] = number
        if dict(changed) != expected:
            raise IdentifierRenameError(f"Only part of a declared rename was applied: {slot}")
        if _exact_identifier_substitution(before, old_tokens, names) != after:
            raise IdentifierRenameError(
                f"A declared rename changed whitespace, line endings or other source bytes: {slot}")
        confirmed[slot] = dict(changed)
    return confirmed
