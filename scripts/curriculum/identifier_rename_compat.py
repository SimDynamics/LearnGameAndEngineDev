"""Conservative, author-declared C++ identifier changes in existing lesson sources.

This is a lexical guard, NOT a compiler, name-resolution engine or permission to
update published state. Scope/binding and program equivalence still require the
author's C++ build, output checks and an authenticated state-layout transition.
The source-format guard remains independent and continues rejecting renames.
"""
from __future__ import annotations

from collections import Counter
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
""".split())


def _declared_name(name: object) -> bool:
    return (type(name) is str and _IDENTIFIER.fullmatch(name) is not None
            and name not in _CPP_KEYWORDS and not name.startswith("__"))


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
                or source_format.source_projection(revised, original) != original):
            raise IdentifierRenameError(
                "A rename cannot modify answers, metadata, file paths or other fields")
    except (ValueError, KeyError, TypeError, IndexError) as error:
        if isinstance(error, IdentifierRenameError):
            raise
        raise IdentifierRenameError(f"Invalid source owner: {error}") from error

    if not renames.keys() <= old_slots.keys():
        raise IdentifierRenameError("The rename declares an unknown code slot")

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
        confirmed[slot] = dict(changed)
    return confirmed
