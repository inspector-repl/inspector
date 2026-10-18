import os
from pathlib import Path


def get_socket_directory() -> Path:
    """Must match socketPath() in lib/prompt.cpp."""
    base = os.environ.get("XDG_RUNTIME_DIR") or os.environ.get("TMPDIR") or "/tmp"
    return Path(base) / "inspector"
