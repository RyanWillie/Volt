# Connectivity Reference

**Source:** `python/volt/design.py`, `examples/timer_555_led_blinker/components.py`

This reference covers net creation, pin access, and the `+=` connectivity operator. All forms lower to kernel mutations. Never connect pins through schematic wires or PCB copper — the logical circuit must own connectivity.

---

## Creating Nets

```python
# Signal net (default kind)
out = design.net("OUT")

# Power net with nominal voltage
vcc = design.net("+5V", kind="power", voltage=5.0)

# Ground net
gnd = design.net("GND", kind="ground")
```

**`design.net(name, *, kind="signal", voltage=None)`**

| Argument | Type | Default | Meaning |
|---|---|---|---|
| `name` | `str` | required | Stable logical net name |
| `kind` | `str` | `"signal"` | `"signal"`, `"power"`, or `"ground"` |
| `voltage` | `float \| None` | `None` | Nominal voltage in volts; lowers into a kernel typed attribute |

Duplicate net names raise an exception.

---

## Net Classes

`design.net_class(...)` creates a kernel-owned routing constraint set, deriving IPC rule values when `current` and `temp_rise` are supplied:

```python
power_class = design.net_class(
    current=1.5,
    temp_rise=10.0,
    copper_weight=1.0,
)
power_class.assign(vcc, gnd)
```

**Selected `design.net_class(...)` arguments:**

| Argument | Type | Default | Meaning |
|---|---|---|---|
| `name` | `str \| None` | auto | Name; auto-assigned if omitted |
| `current` | `float \| None` | `None` | Amps; triggers IPC trace-width derivation |
| `temp_rise` | `float` | `10.0` | Celsius allowable rise |
| `copper_weight` | `float` | `1.0` | oz/ft² |
| `track_width` | `float \| None` | `None` | Override in mm |
| `clearance` | `float \| None` | `None` | Override in mm |
| `voltage` | `float \| None` | `None` | Clearance voltage in volts |
| `default_for` | `str \| None` | `None` | Default class for `"power"` or `"signal"` kind |

`nc.assign(*nets)` attaches the class to one or more `Net` objects.

---

## Pin Access

```python
component[1]         # integer → pin by number
component[2]         # second numeric pin
component["VCC"]     # string → pin by name
component["A"]       # named pin for LEDs etc.
```

Both subscript forms return the same kernel `Pin` handle. The 555 blinker uses both:

```python
# Numeric access (passives)
nets["+5V"] += parts["RA"][1]        # resistor pin 1 (int)
nets["DISCH"] += parts["RA"][2]      # resistor pin 2

# Named access (IC and polarized)
nets["+5V"] += timer["VCC"]
nets["LED_A"] += parts["DLED"]["A"]
```

---

## The `+=` Operator — All Forms

`net += pin` is the canonical connection API. The net is on the left; one or more pin handles are on the right.

**Single pin:**
```python
nets["OUT"] += timer["OUT"]
```

**Tuple of pins (connected in one statement):**
```python
nets["+5V"] += (
    parts["J1"][1],
    timer["VCC"],
    timer["RESET"],
    parts["RA"][1],
    parts["CDEC"][1],
)
```

**Two-item tuple without parentheses:**
```python
nets["DISCH"] += timer["DISCH"], parts["RA"][2], parts["RB"][1]
```

All forms lower to the same kernel mutation. The pin may appear on at most one net; connecting it to a second net raises an exception.

---

## Verbatim Connectivity Block — 555 Blinker

The following is the complete connectivity section from `examples/timer_555_led_blinker/components.py` (starting at line 162):

```python
timer = parts["U1"]
nets["+5V"] += (
    parts["J1"][1],
    timer["VCC"],
    timer["RESET"],
    parts["RA"][1],
    parts["CDEC"][1],
)
nets["DISCH"] += timer["DISCH"], parts["RA"][2], parts["RB"][1]
nets["TIMING"] += timer["TRIG"], timer["THRESH"], parts["RB"][2], parts["CT"][1]
nets["CTRL"] += timer["CTRL"], parts["CCTRL"][1]
nets["OUT"] += timer["OUT"], parts["RLED"][1]
nets["LED_A"] += parts["RLED"][2], parts["DLED"]["A"]
nets["GND"] += (
    parts["J1"][2],
    timer["GND"],
    parts["CT"][2],
    parts["CCTRL"][2],
    parts["CDEC"][2],
    parts["DLED"]["K"],
)
```

This block mixes named-pin access (`timer["VCC"]`), numeric-pin access (`parts["J1"][1]`), two-item comma form, and multi-item parenthesised tuple form — all valid.

---

## Kernel-Only Connectivity Rule

Connectivity belongs in the logical circuit. Do not let schematic wires or PCB copper define it:

- `Schematic.place(...)`, `sch.wire(...)`, `sch.label(...)` — visualize existing nets; never create or merge them.
- `Board.place(...)`, routing copper — implement existing nets; never define the netlist.

If connectivity is ambiguous when looking only at the logical circuit JSON, it is wrong.
