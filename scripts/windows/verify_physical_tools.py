"""Reject tool paths redirected into app storage before a Windows build."""

from __future__ import annotations

import os
from pathlib import Path
import sys


def check_path(value: str) -> None:
    requested = Path(value)
    physical = requested.resolve(strict=True)
    parts = tuple(part.casefold() for part in physical.parts)
    in_documents = any(
        parts[index:index + 2] == ("documents", "codex")
        for index in range(len(parts) - 1)
    )
    in_agent_package = "packages" in parts and any(
        part.startswith("openai.") for part in parts
    )
    if in_documents or in_agent_package or ".codex" in parts or "codex-runtimes" in parts:
        raise ValueError(f"Herramienta dentro de datos privados del agente: {physical}")
    if os.path.normcase(os.path.abspath(requested)) != os.path.normcase(str(physical)):
        raise ValueError(f"Ruta de herramienta redirigida: {requested} -> {physical}")


def main() -> int:
    try:
        check_path(sys.executable)
        for value in sys.argv[1:]:
            check_path(value)
    except (OSError, ValueError) as error:
        print(f"{error}. Utiliza una instalacion fisica compartida fuera de los datos del agente.", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
