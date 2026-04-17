---
name: eda-operator
description: Drive the volt-eda CLI to create and modify hardware projects. Use when building circuits, schematics, PCB layouts, running DRC, or exporting manufacturing files.
---

# EDA Operator

You have access to `volt-eda`, a command-line EDA tool. All commands produce JSON output. Always use `--project .` when operating on the current project.

## Workflow

The standard workflow is: **Circuit → Schematic → Board → Validate → Export**

**IMPORTANT:**
- Treat the current working directory as the intended project root.
- Use `--project .` for all project operations.
- **Do not create projects in `/tmp` or any other external directory.**
- `volt-eda new` creates a **new directory** and cannot initialize the existing `.` directory in place.
- If `volt.json` is missing, do **not** silently create a project somewhere else. Instead, report that the current directory is not initialized as a Volt project and ask the user/app to initialize it explicitly.

### 1. Project Setup

```bash
# Inspect existing project first
volt-eda inspect --project .
```

If `volt-eda inspect --project .` fails because `volt.json` is missing, stop and explain that the current directory is not yet a Volt project. Only use `volt-eda new` when the user explicitly wants a new project directory created at a specific path inside the allowed workspace.

### 2. Circuit — Components & Nets

```bash
# Add a simple 2-pin passive (resistor, capacitor, etc.)
volt-eda component add --project . --name R1 --value 10k --simple-passive
volt-eda component add --project . --name C1 --value 100nF --simple-passive --prefix C

# Add from library (after importing KiCad symbols)
volt-eda component add --project . --name U1 --value "ATtiny85" --lib-component <uuid> --lib-variant <uuid>

# List components
volt-eda component list --project .

# Create nets and connect
volt-eda net add --project . --name VCC
volt-eda net add --project . --name GND
volt-eda net connect --project . --component R1 --pin 1 --net VCC
volt-eda net connect --project . --component R1 --pin 2 --net GND

# List nets
volt-eda net list --project .
```

### 3. Import KiCad Libraries

```bash
# Import symbols (.kicad_sym)
volt-eda import kicad-symbols --file /path/to/library.kicad_sym --project .

# Import footprints (.kicad_mod)
volt-eda import footprint --file /path/to/footprint.kicad_mod --project .

# Search project library
volt-eda library search --project . --query "resistor"
```

### 4. Schematic

```bash
# Auto-place all components (recommended first step)
volt-eda schematic autoplace --project .

# Or place manually (grid coordinates, 1 unit = 2.54mm)
volt-eda schematic place --project . --component R1 --grid "4,4"
volt-eda schematic wire --project . --net VCC --from "R1:1" --to "C1:1"

# Render to check visually
volt-eda schematic render --project . --output schematic.svg
```

### 5. Board Layout

```bash
# Initialize board from circuit (creates device entries)
volt-eda board init --project .

# Set board outline
volt-eda board outline --project . --rect 50x30

# Auto-place devices
volt-eda board autoplace --project .

# Or place manually (mm coordinates)
volt-eda board place --project . --component R1 --x 15 --y 10
volt-eda board move --project . --component R1 --x 20 --y 12 --rotation 90

# Route traces
volt-eda board trace --project . --net VCC --from "R1:1" --to "C1:1" --layer top
volt-eda board trace --project . --net GND --from "R1:2" --to "C1:2" --layer top

# Add vias for layer changes
volt-eda board via --project . --net GND --x 25 --y 15 --drill 0.3 --from-layer top --to-layer bottom

# Add copper pour (ground plane)
volt-eda board plane --project . --net GND --layer bottom --vertices "0,0;50,0;50,30;0,30" --priority 0 --connect-style thermal

# Add mounting holes
volt-eda board hole --project . --x 3 --y 3 --diameter 3.2
volt-eda board hole --project . --x 47 --y 27 --diameter 3.2

# Check unrouted connections
volt-eda board ratsnest --project .

# Render for visual check
volt-eda board render --project . --output board.svg
volt-eda board render --project . --output board.html   # 3D view
```

### 6. Validation

```bash
# Electrical rule check (circuit level)
volt-eda erc --project .

# Design rule check (board level)
volt-eda drc --project .
```

**Always run ERC after wiring the circuit and DRC after laying out the board.** Fix all errors before proceeding. Warnings are informational.

### 7. Export Manufacturing Files

```bash
# Gerber files (copper, mask, silkscreen, outline)
volt-eda export gerber --project . --output-dir ./gerber

# Drill files (PTH + NPTH)
volt-eda export drills --project . --output-dir ./gerber

# Bill of Materials
volt-eda export bom --project . --format csv --output bom.csv

# Pick & place for assembly
volt-eda export pick-place --project . --output pick_place.csv
```

## Key Rules

- **Always use JSON output** — parse it to check `"status": "ok"` or `"passed": true/false`
- **Pin names for simple passives** are `1` and `2`
- **Board coordinates** are in millimeters, **schematic grid** is in 2.54mm units
- **Trace endpoints** use `"Component:Pin"` format, e.g. `"R1:1"` or `"U1:VCC"`
- **Layer names**: `top` or `top_copper`, `bottom` or `bottom_copper`, `inner_N`
- **Run ERC/DRC iteratively** — fix issues, re-run, repeat until clean
- **Render after major changes** to visually verify the layout
