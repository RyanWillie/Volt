"""Immutable snapshot helpers for Python facade value objects."""

from __future__ import annotations

from collections.abc import Mapping
from types import MappingProxyType


def _freeze_value(value):
    if isinstance(value, Mapping):
        return MappingProxyType({key: _freeze_value(item) for key, item in value.items()})
    if isinstance(value, (list, tuple)):
        return tuple(_freeze_value(item) for item in value)
    if isinstance(value, set):
        return frozenset(_freeze_value(item) for item in value)
    return value


def _mutable_value(value):
    if isinstance(value, Mapping):
        return {key: _mutable_value(item) for key, item in value.items()}
    if isinstance(value, tuple):
        return tuple(_mutable_value(item) for item in value)
    if isinstance(value, frozenset):
        return {_mutable_value(item) for item in value}
    return value
