#!/usr/bin/env python

import os
import platform
from pathlib import Path


def get_socket_directory() -> Path:
    """Get platform-specific socket directory."""
    system = platform.system()

    if system == "Linux":
        # On Linux, prefer XDG_RUNTIME_DIR (usually /run/user/UID)
        xdg_runtime_dir = os.environ.get("XDG_RUNTIME_DIR")
        if xdg_runtime_dir:
            return Path(xdg_runtime_dir) / "inspector"
        # Fallback to ~/.cache/inspector
        home = os.environ.get("HOME")
        if home:
            return Path(home) / ".cache" / "inspector"
        # Should never happen, but use current directory as last resort
        return Path(".") / ".inspector"

    elif system == "Darwin":  # macOS
        # On macOS, use ~/Library/Application Support for persistent sockets
        home = os.environ.get("HOME")
        if home:
            return Path(home) / "Library" / "Application Support" / "inspector"
        # Fallback to current directory
        return Path(".") / ".inspector"

    elif system == "Windows":
        # On Windows, use %LOCALAPPDATA% for persistent app data
        local_app_data = os.environ.get("LOCALAPPDATA")
        if local_app_data:
            return Path(local_app_data) / "inspector"
        # Fallback to %APPDATA%
        app_data = os.environ.get("APPDATA")
        if app_data:
            return Path(app_data) / "inspector"
        # Fallback to user profile
        user_profile = os.environ.get("USERPROFILE")
        if user_profile:
            return Path(user_profile) / ".inspector"
        return Path(".") / ".inspector"

    else:
        # Generic Unix fallback: use ~/.cache
        home = os.environ.get("HOME")
        if home:
            return Path(home) / ".cache" / "inspector"
        return Path(".") / ".inspector"
