"""Conservative recognition of authored locator removals; never rewrites text.

This is a preservation proof, not a citation formatter or an APA correctness
checker. Ambiguous, quoted, nested or unsupported forms remain protected.
"""
from dataclasses import dataclass
import re
import unicodedata


EXERCISE_TYPES = frozenset({"multiple_choice", "output_prediction", "fill_code", "order_code",
                            "drag_fill_code", "inline_fill_code"})
_SPACE = r"[ \t\r\n]"
_LABEL = (r"(?:p\.|pp\.|párr\.|párrs\.|párrafo(?:s)?|sección|secciones|sec\.|secs\.|"
          r"tabla(?:s)?|capítulo(?:s)?|cap\.|caps\.|figura(?:s)?|fig\.|figs\.|"
          r"apéndice(?:s)?|anexo(?:s)?|artículo(?:s)?|art\.|arts\.|"
          r"page(?:s)?|paragraph(?:s)?|para\.|paras\.|section(?:s)?|chapter(?:s)?|"
          r"table(?:s)?|figure(?:s)?|appendix|appendices|§{1,2})")
_NUMBER = r"(?:[0-9]+(?:\.[0-9]+)*[a-z]?|[ivxlcdm]+)"
_VALUE = rf"{_NUMBER}(?:{_SPACE}*[-–—]{_SPACE}*{_NUMBER})?"
_LOCATOR = rf"{_LABEL}{_SPACE}+{_VALUE}"
_LOCATORS = re.compile(rf"{_LOCATOR}(?:(?:{_SPACE}*,{_SPACE}*|{_SPACE}+(?:y|e|and|et|&){_SPACE}+){_LOCATOR})*", re.IGNORECASE)
_HEADER = re.compile(r"(?P<head>(?P<author>[^0-9()]+?),[ \t\r\n]+[0-9]{4}[a-z]?)(?P<tail>.*)\Z", re.DOTALL)
_RAW_STRING = re.compile(r'(?:u8|u|U|L)?R"([^\s()\\]{0,16})\(')
_QUOTES = {'"': '"', "'": "'", "“": "”", "‘": "’", "«": "»"}
_CLOSING_QUOTES = frozenset(_QUOTES.values())
_MAX_SOURCE_BYTES = 8 * 1024 * 1024
_CALLABLE = re.compile(r"[^\W\d]\w*(?:\s*(?:::|->|\.)\s*[^\W\d]\w*)*", re.UNICODE)
_CODE_PREFIX_WORDS = frozenset("""alignas alignof and and_eq asm auto bitand bitor bool break case catch
    char char8_t char16_t char32_t class compl concept const consteval constexpr constinit const_cast continue
    co_await co_return co_yield decltype default delete do double dynamic_cast else enum explicit export extern
    false float for friend goto if inline int long mutable namespace new noexcept not not_eq nullptr operator or
    or_eq private protected public register reinterpret_cast requires return short signed sizeof static static_assert
    static_cast struct switch template this thread_local throw true try typedef typeid typename union unsigned using
    virtual void volatile wchar_t while xor xor_eq final override import module""".split())
_ALTERNATIVE_OPERATORS = frozenset({"and", "and_eq", "bitand", "bitor", "compl", "not", "not_eq", "or", "or_eq", "xor", "xor_eq"})


@dataclass
class _Component:
    separator: str
    head: str
    tail: str


def _author(value):
    value = value.strip(" \t\r\n")
    return bool(value) and any(c.isupper() for c in value) and all(
        unicodedata.category(c)[0] in {"L", "M"} or c in " \t\r\n.,&-'’" for c in value)


def _locators(value):
    return _LOCATORS.fullmatch(value.strip(" \t\r\n")) is not None


def _components(value):
    result = []
    for index, part in enumerate(value.split(";")):
        found = _HEADER.fullmatch(part)
        if found and _author(found["author"]):
            tail = found["tail"]
            if tail and (not tail.startswith(",") or not _locators(tail[1:])):
                return None
            result.append(_Component(";" if index else "", found["head"], tail))
        elif result and _locators(part):
            # A semicolon may continue the same author's section/paragraph
            # locators. It becomes a separator between authors only when a new
            # complete author/year header is present.
            result[-1].tail += ";" + part
        else:
            return None
    return result or None


def _word(c):
    return c.isalnum() or c == "_"


def _protected_end(source, at):
    """Return the exclusive quoted/code end, None if absent, -1 if malformed."""
    raw = _RAW_STRING.match(source, at)
    if raw:
        end = source.find(")" + raw[1] + '"', raw.end())
        return -1 if end < 0 else end + len(raw[1]) + 2
    c = source[at]
    if c == "`" or source.startswith("~~~", at):
        width = 1
        while at + width < len(source) and source[at + width] == c:
            width += 1
        end = source.find(c * width, at + width)
        return -1 if end < 0 else end + width
    if source.startswith("//", at):
        end = source.find("\n", at + 2)
        return len(source) if end < 0 else end
    if source.startswith("/*", at):
        end = source.find("*/", at + 2)
        return -1 if end < 0 else end + 2
    if c not in _QUOTES:
        return None
    # Apostrophes inside names/words are not quotation delimiters.
    if c in {"'", "‘"} and at and at + 1 < len(source) and _word(source[at - 1]) and _word(source[at + 1]):
        return None
    closing, cursor = _QUOTES[c], at + 1
    while cursor < len(source):
        if source[cursor] == "\\":
            cursor += 2
        elif source[cursor] == closing:
            return cursor + 1
        else:
            cursor += 1
    return -1


def _ambiguous_code_prefix(source, stop):
    # Consider only the current prose clause, masking earlier quoted/code
    # intervals. A lone callable or an operator/return expression is not enough
    # evidence that a following parenthesis is an optional prose citation.
    start = source.rfind("\n", 0, stop) + 1
    cursor, visible = start, []
    while cursor < stop:
        protected = _protected_end(source, cursor)
        if protected is not None:
            if protected < 0 or protected > stop:
                return True
            visible.append(" ")
            cursor = protected
        else:
            visible.append(source[cursor])
            cursor += 1
    clause = re.split(r"[.!?;]", "".join(visible))[-1].strip()
    if not clause:
        return False
    if _CALLABLE.fullmatch(clause):
        return True
    if any(c in "=+*/%<>[]{}|&^~?\\" for c in clause) or "->" in clause or "::" in clause or re.search(r"\s-\s", clause):
        return True
    tokens = re.findall(r"[^\W\d]\w*", clause)
    return bool(tokens) and (tokens[0] in _CODE_PREFIX_WORDS or any(token in _ALTERNATIVE_OPERATORS for token in tokens))


def _citations(source):
    result, at = [], 0
    while at < len(source):
        protected = _protected_end(source, at)
        if protected is not None:
            if protected < 0:
                return None
            at = protected
            continue
        if source[at] == ")":
            return None
        if source[at] != "(":
            at += 1
            continue
        start, cursor, depth, nested = at, at + 1, 1, False
        while cursor < len(source) and depth:
            protected = _protected_end(source, cursor)
            if protected is not None:
                if protected < 0:
                    return None
                cursor = protected
                continue
            if source[cursor] == "(":
                depth += 1
                nested = True
            elif source[cursor] == ")":
                depth -= 1
            cursor += 1
        if depth:
            return None
        # Parenthetical references must be independent prose, not function
        # arguments or an immediately quoted passage whose locator is required.
        preceding = source[:start].rstrip(" \t\r\n")
        boundary = (not start or source[start - 1].isspace()) and (
            not preceding or preceding[-1] not in _CLOSING_QUOTES)
        following = source[cursor:cursor + 1]
        boundary = boundary and (not following or following.isspace() or following in ".,!?…")
        boundary = boundary and not _ambiguous_code_prefix(source, start)
        if not nested and boundary:
            parsed = _components(source[start + 1:cursor - 1])
            if parsed:
                result.append((start, cursor, parsed))
        at = cursor
    return result


def only_parenthetical_locator_removal(before, after):
    """True only for one or more complete locator removals in safe citations."""
    if not isinstance(before, str) or not isinstance(after, str) or not before.strip() or not after.strip() or before == after:
        return False
    try:
        if max(len(before.encode("utf-8")), len(after.encode("utf-8"))) > _MAX_SOURCE_BYTES:
            return False
    except UnicodeError:
        return False
    old, new = _citations(before), _citations(after)
    if old is None or new is None or len(old) != len(new):
        return False
    previous_old = previous_new = 0
    removed = False
    for (start, end, components), (current_start, current_end, current_components) in zip(old, new):
        if before[previous_old:start] != after[previous_new:current_start] or len(components) != len(current_components):
            return False
        for first, second in zip(components, current_components):
            if first.separator != second.separator or first.head != second.head:
                return False
            if first.tail == second.tail:
                continue
            if first.tail and not second.tail:
                removed = True
            else:
                return False
        previous_old, previous_new = end, current_end
    return removed and before[previous_old:] == after[previous_new:]


def _exact_json_equal(first, second):
    if type(first) is not type(second):
        return False
    if isinstance(first, dict):
        return first.keys() == second.keys() and all(_exact_json_equal(value, second[key]) for key, value in first.items())
    if isinstance(first, list):
        return len(first) == len(second) and all(_exact_json_equal(a, b) for a, b in zip(first, second))
    return first == second


def is_bibliography_only_revision(old_block, new_block):
    """A sole authored /explanation change; every other JSON field is exact."""
    if not isinstance(old_block, dict) or not isinstance(new_block, dict) or old_block.get("type") not in EXERCISE_TYPES:
        return False
    if not _exact_json_equal({k: v for k, v in old_block.items() if k != "explanation"},
                             {k: v for k, v in new_block.items() if k != "explanation"}):
        return False
    return only_parenthetical_locator_removal(old_block.get("explanation"), new_block.get("explanation"))
