# Volt

**Natural language to manufactured hardware product.**

## Repository Structure

```
Volt/
├── eda/        MIT, Rust — EDA engine CLI (volt-eda)
├── cad/        MIT, Rust — CAD engine CLI (volt-cad)
├── agent/      Proprietary — Pi-based agent harness
├── app/        Proprietary — Desktop application
└── VISION.md   Full project vision document
```

## Components

### `eda/` — EDA Engine ([MIT](eda/LICENSE))

A fully self-contained EDA CLI designed for programmatic/agent use.
All commands accept structured input and produce JSON output.

```bash
# Create a project
volt-eda new --name "MyProject"

# Add components and nets
volt-eda component add --project ./MyProject --name R1 --value "10k" --simple-passive
volt-eda net add --project ./MyProject --name VCC
volt-eda net connect --project ./MyProject --component R1 --pin 1 --net VCC

# Validate
volt-eda erc --project ./MyProject

# Layout schematic
volt-eda schematic place --project ./MyProject --component R1 --grid "4,4" --rotation 90
volt-eda schematic wire --project ./MyProject --net VCC --from "R1:1" --to "C1:1"

# Render for visual feedback
volt-eda schematic render --project ./MyProject --output schematic.svg
```

**Status:** Circuit manipulation, ERC (8 rules), schematic editing with Manhattan routing, SVG rendering.

### `cad/` — CAD Engine (MIT)

Agent-friendly CLI (`volt-cad`) around FreeCAD's headless engine for solid modeling, import/export, meshing, and geometric checks.

**Status:** Initial `volt-cad` CLI with FreeCAD-backed operations (see `cad/README.md`).

### `agent/` — Agent Harness (Proprietary)

Pi-based agent runtime with specialized skills for hardware product design.

**Status:** Not yet started.

### `app/` — Desktop Application (Proprietary)

Tauri desktop application with schematic viewer, PCB viewer, 3D viewer, and agent chat interface.

**Status:** Not yet started.

## Building

```bash
# Build the EDA engine
cd eda && cargo build

# Run tests
cd eda && cargo test

# Run the CLI
cd eda && cargo run --bin volt-eda -- --help

# Build the CAD engine
cd cad && cargo build

# Run the CAD CLI
cd cad && cargo run --bin volt-cad -- --help
```

## License

- `eda/` and `cad/` are licensed under [MIT](eda/LICENSE)
- `agent/` and `app/` are proprietary

See [VISION.md](VISION.md) for the full project vision.
