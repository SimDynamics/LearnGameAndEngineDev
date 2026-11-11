"""Conservative source-format-only comparisons for curricular C++ and CMake.

The comparator is NOT a compiler or a semantic equivalence prover. It accepts
only changes that leave every lexical token, literal, comment, preprocessor
directive, file identity and answer slot unchanged. Unknown file types are
immutable. Historical snapshots remain the authority for state identities.
"""
from __future__ import annotations

from copy import deepcopy
from pathlib import PurePosixPath

class SourceFormatError(ValueError):
    pass

_CODE_EXT = {".cpp", ".cc", ".cxx", ".c", ".h", ".hpp", ".hh", ".hxx", ".inl", ".ixx"}
_WS = " \t\r\n\v\f"
_CPP_OPERATORS = tuple(sorted((
    "%:%:", "<<=", ">>=", "<=>", "->*", "...", "##", "::", "->", ".*",
    "++", "--", "==", "!=", "<=", ">=", "&&", "||", "<<", ">>", "+=",
    "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<:", ":>", "<%", "%>",
    "%:", ".*"), key=len, reverse=True))
_RAW_PREFIX = ("u8R\"", "uR\"", "UR\"", "LR\"", "R\"")
_STRING_PREFIX = ("u8\"", "u\"", "U\"", "L\"", "\"",
                  "u8'", "u'", "U'", "L'", "'")

def _literal_end(src: str, at: int, quote_at: int) -> int:
    quote = src[quote_at]
    i = quote_at + 1
    while i < len(src):
        if src[i] == "\\": i += 2
        elif src[i] == quote: return i + 1
        else: i += 1
    raise SourceFormatError("Unterminated C++ literal")

def _cpp_tokens(src: str) -> tuple[str, ...]:
    tokens: list[str] = []
    i = 0
    while i < len(src):
        if src[i] in _WS:
            i += 1
            continue
        # Physical preprocessor lines, including backslash continuations, are
        # immutable inside the directive; blank lines around them may change.
        line = src.rfind("\n", 0, i) + 1
        if src[i] == "#" and not src[line:i].strip():
            end = i
            while True:
                newline = src.find("\n", end)
                if newline < 0:
                    end = len(src)
                    break
                physical = src[end:newline].removesuffix("\r")
                end = newline + 1
                if not physical.endswith("\\"): break
            tokens.append("directive:" + src[i:end].rstrip("\r\n"))
            i = end
            continue
        if src.startswith("//", i):
            end = src.find("\n", i + 2)
            if end < 0: end = len(src)
            tokens.append("comment:" + src[i:end].removesuffix("\r"))
            i = end
            continue
        if src.startswith("/*", i):
            end = src.find("*/", i + 2)
            if end < 0: raise SourceFormatError("Unterminated C++ block comment")
            tokens.append("comment:" + src[i:end + 2])
            i = end + 2
            continue
        matched = False
        for prefix in _RAW_PREFIX:
            if src.startswith(prefix, i):
                begin = i + len(prefix)
                paren = src.find("(", begin)
                if paren < 0 or paren - begin > 16: raise SourceFormatError("Malformed raw literal")
                marker = src[begin:paren]
                if any(c in " \t\r\n\\()" for c in marker):
                    raise SourceFormatError("Malformed raw delimiter")
                close = src.find(")" + marker + '"', paren + 1)
                if close < 0: raise SourceFormatError("Unterminated raw literal")
                end = close + len(marker) + 2
                tokens.append("literal:" + src[i:end]); i = end; matched = True; break
        if matched: continue
        for prefix in _STRING_PREFIX:
            if src.startswith(prefix, i):
                end = _literal_end(src, i, i + len(prefix) - 1)
                tokens.append("literal:" + src[i:end]); i = end; matched = True; break
        if matched: continue
        ch = src[i]
        if ch.isalpha() or ch == "_" or ord(ch) >= 128:
            end = i + 1
            while end < len(src) and (src[end].isalnum() or src[end] == "_" or ord(src[end]) >= 128):
                end += 1
            tokens.append("id:" + src[i:end]); i = end; continue
        if ch.isdigit() or (ch == "." and i + 1 < len(src) and src[i+1].isdigit()):
            end = i + 1
            while end < len(src):
                c = src[end]
                if c.isalnum() or c in "_.'":
                    end += 1
                elif c in "+-" and src[end-1] in "eEpP":
                    end += 1
                else:
                    break
            tokens.append("number:" + src[i:end]); i = end; continue
        operator = next((op for op in _CPP_OPERATORS if src.startswith(op, i)), None)
        if operator:
            tokens.append("op:" + operator); i += len(operator)
        else:
            tokens.append("op:" + ch); i += 1
    return tuple(tokens)

def _cmake_tokens(src: str) -> tuple[str, ...]:
    tokens: list[str] = []
    i = 0
    while i < len(src):
        if src[i] in _WS: i += 1; continue
        # Bracket arguments and comments are indivisible: newlines within a
        # bracket body are part of the supplied value, not layout.
        comment = src[i] == "#"
        start = i + 1 if comment else i
        if start < len(src) and src[start] == "[":
            at = start + 1
            while at < len(src) and src[at] == "=": at += 1
            if at < len(src) and src[at] == "[":
                ending = "]" + src[start+1:at] + "]"
                close = src.find(ending, at + 1)
                if close < 0: raise SourceFormatError("Unterminated CMake bracket")
                end = close + len(ending)
                tokens.append("bracket:" + src[i:end]); i = end; continue
        if comment:
            end = src.find("\n", i)
            if end < 0: end = len(src)
            tokens.append("comment:" + src[i:end].removesuffix("\r"))
            i = end; continue
        if src[i] == '"':
            end = i + 1
            while end < len(src):
                if src[end] == "\\": end += 2
                elif src[end] == '"': end += 1; break
                else: end += 1
            else: raise SourceFormatError("Unterminated CMake quoted argument")
            tokens.append("quoted:" + src[i:end]); i = end; continue
        if src[i] in "()":
            tokens.append("paren:" + src[i]); i += 1; continue
        end = i
        while end < len(src) and src[end] not in _WS + "()#\"":
            if src[end] == "\\":
                if end + 1 >= len(src): raise SourceFormatError("Trailing CMake escape")
                end += 2
            else: end += 1
        if end == i: raise SourceFormatError("Unexpected CMake delimiter")
        tokens.append("word:" + src[i:end]); i = end
    return tuple(tokens)

def _language(path: str) -> str | None:
    name = PurePosixPath(path).name
    if name == "CMakeLists.txt" or path.endswith(".cmake"): return "cmake"
    if PurePosixPath(path).suffix.lower() in _CODE_EXT: return "cpp"
    return None

def _block_language(block: dict) -> str | None:
    authored = block.get("language", "cpp")
    if not isinstance(authored, str):
        return None
    language = authored.strip().lower()
    if language in {"cpp", "c++", "cxx", "c", "h", "hpp"}:
        return "cpp"
    if language == "cmake":
        return "cmake"
    return None

def code_slots(block: dict) -> dict[str, tuple[str | None, str]]:
    """Return every authored code location and its language, not answer fields."""
    if not isinstance(block, dict): raise SourceFormatError("Expected a code block")
    kind = block.get("type")
    language = _block_language(block)
    result: dict[str, tuple[str | None, str]] = {}
    if "code" in block:
        result["code"] = (language, block["code"])
    if kind in ("inline_fill_code", "drag_fill_code") and "template" in block:
        result["template"] = (language, block["template"])
    if kind == "cpp_compiler":
        if "initialCode" in block: result["initialCode"] = ("cpp", block["initialCode"])
        for i, value in enumerate(block.get("projectFiles", [])):
            result[f"projectFiles/{i}/initialCode"] = (_language(value["path"]), value["initialCode"])
    if kind == "order_code":
        for i, text in enumerate(block.get("fragments", [])):
            result[f"fragments/{i}"] = (language, text)
    if kind == "drag_fill_code":
        for i, text in enumerate(block.get("choices", [])):
            result[f"choices/{i}"] = (language, text)
    if not all(type(value) is str for _, value in result.values()):
        raise SourceFormatError("Malformed source field")
    return result

def source_projection(current: dict, original: dict) -> dict:
    """Restore only source bytes; keep every present answer and state field."""
    old_slots, new_slots = code_slots(original), code_slots(current)
    if old_slots.keys() != new_slots.keys() or original.get("type") != current.get("type"):
        raise SourceFormatError("Source slots or owner type changed")
    projected = deepcopy(current)
    for slot, (language, source) in old_slots.items():
        if new_slots[slot][0] != language:
            raise SourceFormatError("Source language or project file changed")
        parts = slot.split("/")
        target = projected
        for part in parts[:-1]:
            target = target[int(part)] if part.isdecimal() else target[part]
        if isinstance(target, list):
            target[int(parts[-1])] = source
        else:
            target[parts[-1]] = source
    return projected

def require_source_lexical(original: dict, current: dict) -> bool:
    """Validate code-only lexical equivalence without assuming prose is fixed."""
    before, after = code_slots(original), code_slots(current)
    if before.keys() != after.keys() or original.get("type") != current.get("type"):
        raise SourceFormatError("Source slots or owner type changed")
    changed = False
    for slot, (language, source) in before.items():
        lang, newer = after[slot]
        if language != lang: raise SourceFormatError("Source language changed")
        if newer == source: continue
        if language is None:
            raise SourceFormatError(f"Unknown project data may not be changed: {slot}")
        # Translation-phase splicing can join an identifier across a line or
        # change the content of a directive. Keep those sources byte-exact.
        if "\\\n" in source or "\\\n" in newer or "\\\r\n" in source or "\\\r\n" in newer:
            raise SourceFormatError(f"Physical line splices cannot be reformatted: {slot}")
        lexer = _cmake_tokens if language == "cmake" else _cpp_tokens
        if lexer(source) != lexer(newer):
            raise SourceFormatError(f"Tokens, literals, comments or directives changed: {slot}")
        changed = True
    return changed

def require_source_format(original: dict, current: dict) -> bool:
    """Reject any non-source change under a new format-only authorization."""
    changed = require_source_lexical(original, current)
    if source_projection(current, original) != original:
        raise SourceFormatError("Another field, answer, file path or evaluation rule changed")
    return changed
