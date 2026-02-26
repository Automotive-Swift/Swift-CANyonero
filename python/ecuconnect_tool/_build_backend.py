"""Thin wrapper around scikit-build-core that syncs C++ sources before sdist."""

from scikit_build_core.build import *  # noqa: F401,F403
from scikit_build_core import build as _sb

import os
import shutil

_HERE = os.path.dirname(os.path.abspath(__file__))
_LIBCAN = os.path.join(_HERE, "..", "..", "Sources", "libCANyonero")

_FILES = [
    (os.path.join(_LIBCAN, "Protocol.cpp"), "csrc/Protocol.cpp"),
    (os.path.join(_LIBCAN, "lz4.c"), "csrc/lz4.c"),
]
_DIRS = [
    (os.path.join(_LIBCAN, "include"), "csrc/include"),
]


def _sync_csrc():
    if not os.path.isdir(_LIBCAN):
        return
    for src, dst in _FILES:
        dst = os.path.join(_HERE, dst)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
    for src, dst in _DIRS:
        dst = os.path.join(_HERE, dst)
        if os.path.exists(dst):
            shutil.rmtree(dst)
        shutil.copytree(src, dst)


def build_sdist(*args, **kwargs):
    _sync_csrc()
    return _sb.build_sdist(*args, **kwargs)


def build_wheel(*args, **kwargs):
    _sync_csrc()
    return _sb.build_wheel(*args, **kwargs)
