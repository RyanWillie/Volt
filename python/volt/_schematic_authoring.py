"""Scoped schematic authoring forwarding helpers."""

from __future__ import annotations

from collections.abc import Callable, Iterable
from typing import Any

from .logical import Net


class _ScopedSchematicAuthoring:
    """Apply presentation authoring scope before delegating to a schematic sheet."""

    def __init__(
        self,
        sheet: Any,
        *,
        local_point: Callable[[Any], Any],
        authored_region: int | None,
        ortho_line_entry_parts: Callable[[Any], tuple[Any, Any, Any]],
        signal_stub_entry_parts: Callable[[Any], tuple[Any, Any, Any]],
        signal_stub_entry_has_anchor: Callable[[Any], bool],
    ):
        self._sheet = sheet
        self._local_point = local_point
        self._authored_region = authored_region
        self._ortho_line_entry_parts = ortho_line_entry_parts
        self._signal_stub_entry_parts = signal_stub_entry_parts
        self._signal_stub_entry_has_anchor = signal_stub_entry_has_anchor

    def place(
        self,
        component: Any,
        *,
        at: Any,
        orient: str,
        symbol: Any = None,
        variant: str = "default",
        reference_label: str | None = None,
    ) -> Any:
        return self._sheet.place(
            component,
            at=self._local_point(at),
            orient=orient,
            symbol=symbol,
            variant=variant,
            reference_label=reference_label,
            _authored_region=self._authored_region,
        )

    def wire(self, net: Net, points: Iterable[Any] | None = None) -> Any:
        if points is None:
            return self._sheet.wire(net, _authored_region=self._authored_region)
        return self._sheet.wire(
            net,
            tuple(self._local_point(point) for point in points),
            _authored_region=self._authored_region,
        )

    def connect(
        self,
        *args: Any,
        net: Net | None = None,
        shape: str | None = None,
        k: float | None = None,
    ) -> Any:
        if args and isinstance(args[0], Net):
            if net is not None:
                raise ValueError("Pass schematic connection net either first or as net=, not both")
            return self._sheet.connect(
                args[0],
                *(self._local_point(point) for point in args[1:]),
                shape=shape,
                k=k,
                _authored_region=self._authored_region,
            )
        return self._sheet.connect(
            *(self._local_point(point) for point in args),
            net=net,
            shape=shape,
            k=k,
            _authored_region=self._authored_region,
        )

    def ortho_lines(
        self,
        entries: Iterable[Any],
        *,
        shape: str | None = None,
        k: float | None = None,
    ) -> Any:
        localized = []
        for entry in entries:
            net, start, end = self._ortho_line_entry_parts(entry)
            localized.append((net, self._local_point(start), self._local_point(end)))
        return self._sheet.ortho_lines(
            localized,
            shape=shape,
            k=k,
            _authored_region=self._authored_region,
        )

    def label(
        self,
        net: Net,
        *,
        at: Any,
        orient: str = "Right",
        label: str | None = None,
    ) -> Any:
        return self._sheet.label(
            net,
            at=self._local_point(at),
            orient=orient,
            label=label,
            _authored_region=self._authored_region,
        )

    def net_label(
        self,
        name_or_net: str | Net,
        *,
        at: Any,
        orient: str = "Right",
        label: str | None = None,
    ) -> Any:
        return self._sheet.net_label(
            name_or_net,
            at=self._local_point(at),
            orient=orient,
            label=label,
            _authored_region=self._authored_region,
        )

    def local_label(
        self,
        name_or_net: str | Net,
        *,
        at: Any,
        side: str = "Right",
        offset: float = 2,
        orient: str | None = None,
    ) -> Any:
        return self._sheet.local_label(
            name_or_net,
            at=self._local_point(at),
            side=side,
            offset=offset,
            orient=orient,
            _authored_region=self._authored_region,
        )

    def signal_stub(
        self,
        name_or_net: str | Net,
        *,
        at: Any,
        side: str | None = None,
        length: float = 8,
        label_gap: float = 2,
        orient: str | None = None,
        label: str | None = None,
    ) -> Any:
        return self._sheet.signal_stub(
            name_or_net,
            at=self._local_point(at),
            side=side,
            length=length,
            label_gap=label_gap,
            orient=orient,
            label=label,
            _authored_region=self._authored_region,
        )

    def signal_tag(
        self,
        name_or_net: str | Net,
        *,
        at: Any,
        side: str | None = None,
        length: float = 8,
        label: str | None = None,
        kind: str = "Bidirectional",
        orient: str | None = None,
    ) -> Any:
        return self._sheet.signal_tag(
            name_or_net,
            at=self._local_point(at),
            side=side,
            length=length,
            label=label,
            kind=kind,
            orient=orient,
            _authored_region=self._authored_region,
        )

    def signal_tags(
        self,
        items: Iterable[Any],
        *,
        at: Any | None = None,
        default_at: Any | None = None,
        side: str | None = None,
        pitch: float = 8,
        length: float = 8,
        kind: str = "Bidirectional",
        orient: str | None = None,
    ) -> Any:
        entries = self._signal_stub_items(tuple(items))
        base_at = self._signal_group_base_at(entries, at=at, default_at=default_at)
        return self._sheet.signal_tags(
            entries,
            at=base_at,
            side=side,
            pitch=pitch,
            length=length,
            kind=kind,
            orient=orient,
            _authored_region=self._authored_region,
        )

    def signal_stubs(
        self,
        items: Iterable[Any],
        *,
        at: Any | None = None,
        default_at: Any | None = None,
        side: str | None = None,
        pitch: float = 8,
        length: float = 8,
        label_gap: float = 2,
        orient: str | None = None,
    ) -> Any:
        entries = self._signal_stub_items(tuple(items))
        base_at = self._signal_group_base_at(entries, at=at, default_at=default_at)
        return self._sheet.signal_stubs(
            entries,
            at=base_at,
            side=side,
            pitch=pitch,
            length=length,
            label_gap=label_gap,
            orient=orient,
            _authored_region=self._authored_region,
        )

    def junction(self, net: Net, *, at: Any) -> Any:
        return self._sheet.junction(
            net,
            at=self._local_point(at),
            _authored_region=self._authored_region,
        )

    def terminal(
        self,
        name_or_net: str | Net | None = None,
        *,
        at: Any,
        net: Net | None = None,
        kind: str = "Power",
        orient: str | None = None,
    ) -> Any:
        return self._sheet.terminal(
            name_or_net,
            at=self._local_point(at),
            net=net,
            kind=kind,
            orient=orient,
            _authored_region=self._authored_region,
        )

    def terminal_stub(
        self,
        name_or_net: str | Net | None = None,
        *,
        at: Any,
        net: Net | None = None,
        kind: str = "Power",
        side: str | None = None,
        length: float = 8,
        orient: str | None = None,
    ) -> Any:
        return self._sheet.terminal_stub(
            name_or_net,
            at=self._local_point(at),
            net=net,
            kind=kind,
            side=side,
            length=length,
            orient=orient,
            _authored_region=self._authored_region,
        )

    def power(
        self,
        name: str,
        *,
        at: Any,
        net: Net | None = None,
        orient: str = "Up",
    ) -> Any:
        return self._sheet.power(
            name,
            at=self._local_point(at),
            net=net,
            orient=orient,
            _authored_region=self._authored_region,
        )

    def power_stub(
        self,
        name: str,
        *,
        at: Any,
        net: Net | None = None,
        side: str = "Up",
        length: float = 8,
        orient: str = "Up",
    ) -> Any:
        return self._sheet.power_stub(
            name,
            at=self._local_point(at),
            net=net,
            side=side,
            length=length,
            orient=orient,
            _authored_region=self._authored_region,
        )

    def ground(
        self,
        name: str | None = None,
        *,
        at: Any,
        net: Net | None = None,
        orient: str = "Down",
    ) -> Any:
        return self._sheet.ground(
            name,
            at=self._local_point(at),
            net=net,
            orient=orient,
            _authored_region=self._authored_region,
        )

    def ground_stub(
        self,
        name: str | None = None,
        *,
        at: Any,
        net: Net | None = None,
        side: str = "Down",
        length: float = 8,
        orient: str = "Down",
    ) -> Any:
        return self._sheet.ground_stub(
            name,
            at=self._local_point(at),
            net=net,
            side=side,
            length=length,
            orient=orient,
            _authored_region=self._authored_region,
        )

    def sheet_port(
        self,
        name: str,
        *,
        at: Any,
        net: Net | None = None,
        kind: str = "Bidirectional",
        orient: str = "Right",
    ) -> Any:
        return self._sheet.sheet_port(
            name,
            at=self._local_point(at),
            net=net,
            kind=kind,
            orient=orient,
            _authored_region=self._authored_region,
        )

    def off_page(
        self,
        name: str,
        *,
        at: Any,
        net: Net | None = None,
        orient: str = "Right",
    ) -> Any:
        return self._sheet.off_page(
            name,
            at=self._local_point(at),
            net=net,
            orient=orient,
            _authored_region=self._authored_region,
        )

    def no_connect(
        self,
        anchor: Any,
        *,
        orient: str = "Right",
        offset: float = 0,
        reason: str | None = None,
    ) -> Any:
        return self._sheet.no_connect(
            anchor,
            orient=orient,
            offset=offset,
            reason=reason,
            _authored_region=self._authored_region,
        )

    def _signal_stub_items(self, items: tuple[Any, ...]) -> tuple[Any, ...]:
        entries = []
        for item in items:
            name_or_net, anchor, label = self._signal_stub_entry_parts(item)
            if anchor is None:
                entries.append((name_or_net, label) if label is not None else name_or_net)
            elif label is None:
                entries.append((name_or_net, self._local_point(anchor)))
            else:
                entries.append((name_or_net, self._local_point(anchor), label))
        return tuple(entries)

    def _signal_group_base_at(
        self,
        entries: tuple[Any, ...],
        *,
        at: Any | None,
        default_at: Any | None,
    ) -> Any | None:
        base_at = self._local_point(at) if at is not None else None
        if (
            base_at is None
            and default_at is not None
            and any(not self._signal_stub_entry_has_anchor(item) for item in entries)
        ):
            base_at = default_at
        return base_at
