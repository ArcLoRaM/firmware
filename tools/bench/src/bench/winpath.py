"""WSL <-> Windows paths for the Windows tools bench drives through WSL interop."""

from __future__ import annotations

import re
from pathlib import PurePosixPath, PureWindowsPath

_MNT_RE = re.compile(r"^/mnt/([a-zA-Z])(/.*)?$")


def to_windows(path: str | PurePosixPath) -> str:
    """'/mnt/c/Users/x' -> 'C:\\Users\\x'. Only Windows drives: the Build Tree lives on C:."""
    m = _MNT_RE.match(str(path))
    if not m:
        raise ValueError(f"{path} is not on a Windows drive (/mnt/<drive>/...)")
    rest = (m[2] or "/").replace("/", "\\")
    return f"{m[1].upper()}:{rest}"


def to_wsl(path: str) -> PurePosixPath:
    """'C:\\Users\\x' -> '/mnt/c/Users/x'."""
    p = PureWindowsPath(path)
    if not p.drive or len(p.drive) != 2:
        raise ValueError(f"{path} is not a drive path (C:\\...)")
    return PurePosixPath("/mnt", p.drive[0].lower(), *p.parts[1:])
