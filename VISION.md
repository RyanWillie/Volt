# Volt — Vision Document

## One-Line Vision

**Natural language to manufactured hardware product.**

"Create me a PCB for an Alexa clone based on a Raspberry Pi Pico, with two
external speakers, a screen, microphone, USB-C charging, and a battery that
lasts an hour" → schematic → PCB → enclosure → manufacturing files.

**Repos:**
- `volt-eda` — MIT, Rust — EDA engine CLI
- `volt-cad` — MIT, Rust — CAD engine CLI (wraps FreeCAD)
- `volt-app` — Proprietary — Desktop application
- `volt-agent` — Proprietary — Pi-based agent harness

---

## Architecture

Four independent components, two licensing models:

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                 │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │  APPLICATION                              (closed source) │  │
│  │                                                           │  │
│  │  • Project management dashboard                          │  │
│  │  • Interactive schematic viewer / editor                  │  │
│  │  • PCB layout viewer / editor                            │  │
│  │  • 3D product viewer (enclosure + PCB)                   │  │
│  │  • Agent chat interface                                  │  │
│  │  • Manufacturing order flow (JLCPCB, PCBWay)             │  │
│  └───────────────────────────────────────────────────────────┘  │
│                              │                                  │
│  ┌───────────────────────────▼───────────────────────────────┐  │
│  │  AGENT HARNESS (Pi runtime)               (closed source) │  │
│  │                                                           │  │
│  │  Skills:                                                  │  │
│  │   • requirements-decomposer   Parse NL → subsystem specs │  │
│  │   • component-selector        DigiKey/LCSC/Mouser APIs   │  │
│  │   • design-knowledge          Reference circuits & rules  │  │
│  │   • eda-operator              Drive the EDA CLI           │  │
│  │   • cad-operator              Drive the CAD CLI           │  │
│  │   • simulation-runner         ngspice for validation      │  │
│  │   • manufacturing-integrator  BOM pricing, fab ordering   │  │
│  │   • review-iterate            Check results, fix errors   │  │
│  └──────┬──────────────────────────────────┬─────────────────┘  │
│         │                                  │                    │
│  ┌──────▼──────────────────┐  ┌────────────▼─────────────────┐  │
│  │  EDA ENGINE       (MIT) │  │  CAD ENGINE            (MIT) │  │
│  │  Rust CLI               │  │  Rust CLI wrapper            │  │
│  │                         │  │                              │  │
│  │  • Circuit data model   │  │  • Agent-friendly JSON API   │  │
│  │  • LibrePCB .lp format  │  │  • Drives freecadcmd         │  │
│  │  • KiCad lib import     │  │    (LGPL-2.0+, bundleable)   │  │
│  │  • Schematic generation │  │  • Enclosure generation      │  │
│  │  • PCB layout           │  │  • PCB 3D visualization      │  │
│  │  • ERC / DRC            │  │  • STEP / STL export         │  │
│  │  • Basic auto-routing   │  │  • Fit checking              │  │
│  │  • Gerber / Excellon    │  │  • Fillets, chamfers,        │  │
│  │  • BOM / Pick & Place   │  │    assemblies, FEM           │  │
│  │  • D-356 netlist        │  │    (via OpenCASCADE)         │  │
│  │  • Component library    │  │                              │  │
│  │    management           │  │                              │  │
│  └─────────────────────────┘  └──────────────────────────────┘  │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Component Details

### 1. EDA Engine — `volt-eda` (MIT, Rust)

A fully self-contained EDA CLI designed for programmatic/agent use.
All commands accept structured input and produce JSON output.

**Native format:** LibrePCB `.lp` S-Expression files
- Simpler than KiCad (13 top-level types vs KiCad's complex nested format)
- Clean separation: circuit (logical) / schematic (visual) / board (physical)
- Perfect for agents — can build a circuit without placing symbols first

**Library ecosystem access:**
- Import KiCad `.kicad_sym` / `.kicad_mod` files (20,000+ symbols, CC-BY-SA 4.0)
- Import LibrePCB libraries via API (1,800+ devices)
- Local library management with search

**Core capabilities:**
| Capability | Description |
|---|---|
| Circuit manipulation | Add components, create nets, connect signals — JSON in/out |
| ERC | Open nets, unconnected signals, missing gates, naming conflicts (~10 rules) |
| DRC | Clearance, drill, annular ring, keepout, courtyard, silkscreen (~30 rules) |
| Schematic generation | Place symbols, route wires, auto-layout |
| PCB layout | Place footprints, basic auto-routing (2-layer) |
| Gerber/Excellon export | Manufacturing-ready output |
| BOM export | CSV with supplier part numbers |
| Pick & place export | Assembly automation files |
| Netlist export | D-356 for testing |

**Key dependencies (all MIT/Apache-2.0):**
| Crate | Purpose |
|---|---|
| `geo` | Computational geometry for DRC (polygon boolean, clearance) |
| `gerber-types` | Gerber RS-274X code generation |
| `clap` | CLI framework |
| `serde` / `serde_json` | JSON I/O for agent integration |
| `nom` or custom | S-Expression parser |

**Example agent interaction:**
```bash
# Create project
volt-eda new --name "AlexaClone" --output ./alexa

# Add a component from library
volt-eda component add --project ./alexa \
  --search "raspberry pi pico" --name "U1"

# Create a net and connect
volt-eda net add --project ./alexa --name "VBUS"
volt-eda net connect --project ./alexa \
  --component "U1" --pin "VBUS" --net "VBUS"

# Validate
volt-eda erc --project ./alexa
# → {"status":"pass","messages":[],"summary":{"errors":0,"warnings":0}}

# Export
volt-eda export gerber --project ./alexa --board default --output ./gerber/
```

---

### 2. CAD Engine — `volt-cad` (MIT, Rust)

An agent-friendly CLI wrapper around FreeCAD's headless engine (`freecadcmd`).
Your code (MIT Rust CLI) translates JSON commands into FreeCAD Python scripts,
executes them via `freecadcmd`, and returns structured JSON results.

**Why FreeCAD (LGPL-2.0+):**
- 20+ years of production-grade CAD development, 22K+ GitHub stars
- Full B-rep modeling via OpenCASCADE kernel
- Fillets, chamfers, threads, lofts — everything needed for real enclosures
- Assembly workbench for multi-part designs
- Industry-grade STEP/IGES/STL import and export
- KiCad StepUp plugin — imports KiCad PCBs directly into 3D assemblies
- FEM (finite element analysis) for structural validation
- Headless mode (`freecadcmd`) with full Python scripting API
- Parametric spreadsheet-driven dimensions
- Huge community, documentation, tutorials

**Licensing & bundling:**
- FreeCAD is **LGPL-2.0+**, not GPL — explicitly allows use in proprietary products
- `volt-cad` (your MIT Rust CLI) calls `freecadcmd` as a **subprocess**
- Process boundary = no linking = no LGPL obligations on your code
- FreeCAD is bundled as a separate binary (same pattern as Electron bundling
  Chromium, or apps bundling FFmpeg)
- LGPL compliance: ship FreeCAD source link + LGPL notice + allow user to
  swap the FreeCAD binary

**Architecture:**
```
volt-cad (MIT, Rust binary)
│
├── Accepts agent-friendly JSON/CLI commands
├── Generates FreeCAD Python scripts (.py)
├── Executes: freecadcmd --console script.py
├── Parses output / reads generated files
└── Returns structured JSON results

freecadcmd (LGPL, bundled separately)
│
├── OpenCASCADE B-rep kernel
├── Part module (solid modeling)
├── Mesh module (STL generation)
├── Assembly module (multi-part)
└── Import/Export (STEP, IGES, STL, OBJ)
```

**Core capabilities:**
| Capability | Description | FreeCAD Module |
|---|---|---|
| Parametric enclosure generation | Box/shell with cutouts for ports, screen, buttons, speakers | Part, PartDesign |
| Fillets & chamfers | Rounded edges for manufacturing and ergonomics | PartDesign |
| PCB mounting | Generate standoffs, screw bosses, snap fits from board outline | Part |
| Component clearance | Ensure tall components fit inside enclosure | Part (boolean check) |
| Port cutouts | USB-C, audio jack, button holes — positioned from PCB data | Part (boolean cut) |
| PCB 3D import | Import PCB as 3D model (with component heights) | KiCad StepUp |
| Assembly | Combine PCB + enclosure + lid into assembly | Assembly |
| STEP export | For mechanical review and injection molding | Import/Export |
| STL export | For 3D printing prototypes | Mesh |
| Fit check | Validate PCB + components fit within enclosure constraints | Part (interference detect) |
| FEM analysis | Structural validation of enclosure (drop test, etc.) | FEM |

**Example agent interaction:**
```bash
# Generate enclosure from PCB data
volt-cad enclosure generate \
  --pcb-outline ./alexa/boards/default/board.lp \
  --wall-thickness 2.0 \
  --clearance 1.5 \
  --fillet-radius 1.0 \
  --output ./alexa/enclosure/

# Add port cutouts
volt-cad enclosure cutout \
  --enclosure ./alexa/enclosure/case.FCStd \
  --type usb-c --face bottom --offset 12.5,0 \
  --output ./alexa/enclosure/case.FCStd

# Add speaker grilles
volt-cad enclosure cutout \
  --enclosure ./alexa/enclosure/case.FCStd \
  --type speaker-grille --face top --diameter 40.0 \
  --pattern hex --hole-size 2.0 \
  --output ./alexa/enclosure/case.FCStd

# Add screw bosses for PCB mounting
volt-cad enclosure mount \
  --enclosure ./alexa/enclosure/case.FCStd \
  --pcb ./alexa/boards/default/board.lp \
  --screw-type m2.5 --standoff-height 3.0 \
  --output ./alexa/enclosure/case.FCStd

# Import PCB as 3D model and assemble
volt-cad assembly create \
  --enclosure ./alexa/enclosure/case.FCStd \
  --pcb-step ./alexa/export/board.step \
  --output ./alexa/enclosure/assembly.FCStd

# Fit check (interference detection)
volt-cad check fit \
  --assembly ./alexa/enclosure/assembly.FCStd
# → {"status":"pass","clearance_min_mm":1.2,"violations":[]}

# Export for manufacturing
volt-cad export step \
  --input ./alexa/enclosure/case.FCStd \
  --output ./alexa/enclosure/case.step

volt-cad export stl \
  --input ./alexa/enclosure/case.FCStd \
  --output ./alexa/enclosure/case.stl
```

---

### 3. Agent Harness (Closed Source, Pi Runtime)

A self-contained agent runtime built on Pi with specialized skills for
hardware product design. This is where the engineering intelligence lives.

**Skills:**

| Skill | Responsibility |
|---|---|
| `requirements-decomposer` | Parse natural language → subsystem breakdown (power, audio, display, wireless, compute, mechanical) |
| `component-selector` | Search DigiKey/LCSC/Mouser APIs, match specs, check availability, optimize cost |
| `design-knowledge` | Reference circuits library — USB-C (CC resistors, ESD protection), battery charging (TP4056 + DW01), I2S audio (MAX98357A), voltage regulation (LDO/switching), RPi Pico boot circuit, decoupling cap placement rules, etc. |
| `eda-operator` | Translate design decisions into `volt-eda` CLI commands, interpret JSON results, iterate on ERC/DRC failures |
| `cad-operator` | Generate enclosure specs, drive `volt-cad` CLI, iterate on fit check failures |
| `simulation-runner` | Generate SPICE netlists, run ngspice (BSD, bundleable), validate power budgets and signal integrity |
| `manufacturing-integrator` | Generate JLCPCB/PCBWay quotes, match BOM to supplier parts, validate assembly feasibility |
| `review-iterate` | Review all outputs, identify issues, loop back to fix — the agent's self-correction loop |

**Agent workflow for "Alexa clone" request:**

```
1. DECOMPOSE
   "Alexa clone on RPi Pico" →
   ├── Compute: Raspberry Pi Pico W (RP2040 + WiFi)
   ├── Audio Output: I2S codec + 2x speaker amplifiers
   ├── Audio Input: MEMS microphone (I2S/PDM)
   ├── Display: SPI TFT (size TBD based on enclosure)
   ├── Power: USB-C input → charge controller → LiPo → 3.3V/5V regulators
   └── Mechanical: enclosure with speaker grilles, screen window, mic hole, USB port

2. SELECT COMPONENTS (DigiKey/LCSC API)
   ├── U1: Raspberry Pi Pico W
   ├── U2: MAX98357A (I2S amp, 2x)
   ├── U3: SPH0645LM4H (I2S MEMS mic)
   ├── U4: ILI9341 2.8" SPI TFT
   ├── U5: TP4056 (LiPo charger)
   ├── U6: DW01A + FS8205 (battery protection)
   ├── U7: AP2112K-3.3 (3.3V LDO)
   ├── BT1: 3.7V 2000mAh LiPo (est ~1hr at full load)
   └── Passives: decoupling caps, CC resistors, etc.

3. BUILD CIRCUIT (volt-eda CLI)
   ├── Create project
   ├── Add all components to library
   ├── Create nets (VBUS, VBAT, 3V3, GND, I2S_SCK, I2S_WS, ...)
   ├── Wire each subsystem per reference circuits
   └── Run ERC → fix issues → re-run

4. SIMULATE (ngspice)
   ├── Power budget: 150mA (Pico) + 2x 1.5W speakers + 80mA (display) + ...
   ├── Battery life: 2000mAh / ~600mA ≈ 3.3hrs (exceeds 1hr requirement ✓)
   └── Voltage regulation: verify LDO dropout under load

5. LAYOUT PCB (volt-eda CLI)
   ├── Create board with outline
   ├── Place components (power section near USB, audio near edge, ...)
   ├── Route traces (auto-route 2-layer)
   └── Run DRC → fix issues → re-run

6. DESIGN ENCLOSURE (volt-cad CLI)
   ├── Generate box from PCB outline + height clearance
   ├── Add cutouts: USB-C, speaker grilles, screen window, mic hole
   ├── Add mounting standoffs
   ├── Run fit check
   └── Export STEP + STL

7. PREPARE MANUFACTURING
   ├── Export Gerber + drill files
   ├── Export BOM matched to LCSC parts
   ├── Export pick & place file
   ├── Get JLCPCB quote
   └── Present complete package to user
```

---

### 4. Application (Closed Source)

Desktop application providing visual interface and agent interaction.

**Technology:** Tauri (Rust backend) or Electron

**Views:**
| View | Description |
|---|---|
| Project Dashboard | All projects, status, cost estimates |
| Agent Chat | Natural language input, step-by-step progress, approval gates |
| Schematic Viewer | SVG-rendered schematics from `volt-eda`, interactive pan/zoom, click-to-inspect |
| PCB Viewer | Board layout visualization, layer toggle, net highlighting |
| 3D Viewer | Combined PCB + enclosure via wgpu/WebGPU rendering |
| BOM Manager | Component list, pricing, supplier links, alternatives |
| Manufacturing | Gerber preview, order submission, tracking |

**Human-in-the-loop gates:**
The agent proposes, the human approves at key checkpoints:
1. Component selection review
2. Schematic review (after ERC passes)
3. PCB layout review (after DRC passes)
4. Enclosure review (after fit check passes)
5. Manufacturing order review (before spending money)

---

## Licensing Strategy

| Component | License | Rationale |
|---|---|---|
| `volt-eda` (EDA engine) | MIT | Attract contributors, build ecosystem, enable integrations |
| `volt-cad` (CAD CLI wrapper) | MIT | Standalone value as agent-friendly CAD interface |
| FreeCAD (bundled CAD kernel) | LGPL-2.0+ | Subprocess — no LGPL obligations on your code |
| Agent harness | Proprietary | Core IP — engineering intelligence, design knowledge, workflow |
| Application | Proprietary | Commercial product — GUI, UX, manufacturing integration |

The MIT tools are independently useful and will attract an open-source community.
The proprietary layer is where the unique value lives — the agent intelligence
that turns a sentence into a manufactured product.

---

## Key Dependencies & Licensing

| Dependency | License | Bundleable | Used by |
|---|---|---|---|
| FreeCAD + OpenCASCADE | LGPL-2.0+ | ✅ (as separate binary) | CAD engine |
| `geo` (geometry) | MIT/Apache-2.0 | ✅ | EDA engine (DRC) |
| `gerber-types` | MIT/Apache-2.0 | ✅ | EDA engine (export) |
| `clap` | MIT/Apache-2.0 | ✅ | Both CLIs |
| `serde` / `serde_json` | MIT/Apache-2.0 | ✅ | Both CLIs |
| `wgpu` | MIT/Apache-2.0 | ✅ | Application (3D viewer) |
| ngspice | BSD | ✅ | Agent (simulation) |
| KiCad libraries | CC-BY-SA 4.0 | ✅ (data) | EDA engine (import) |
| LibrePCB libraries | CC-BY-SA 4.0 | ✅ (data) | EDA engine (import) |

**No GPL dependencies in the bundle.** Everything is MIT, Apache-2.0, LGPL, or BSD.
LGPL components (FreeCAD) are bundled as separate binaries called via subprocess —
no linking, no LGPL obligations on your proprietary code.

---

## Development Phases

### Phase 1 — EDA Foundation (Weeks 1-6)
Build `volt-eda` to the point where an agent can create a circuit and validate it.
- S-Expression parser/writer
- Core data model (circuit, schematic, board)
- LibrePCB `.lp` read/write with round-trip fidelity
- KiCad `.kicad_sym` / `.kicad_mod` library importer
- Circuit manipulation CLI (add component, create net, connect)
- ERC engine
- JSON output on all commands
- **Milestone:** Agent creates a voltage divider, passes ERC

### Phase 2 — Agent Proof of Concept (Weeks 4-8, overlapping)
Build the Pi agent harness with enough skills to design a simple circuit.
- Pi skills for `volt-eda` CLI
- Component search skill (DigiKey/LCSC API)
- Design knowledge: basic reference circuits (power supply, LED, resistor networks)
- Workflow engine: decompose → select → build → validate
- **Milestone:** "Design an LED blinker with an ATtiny85" → working schematic

### Phase 3 — PCB & Export (Weeks 7-12)
Complete the physical design pipeline.
- PCB layout in `volt-eda` (component placement, basic routing)
- DRC engine (clearance checks using `geo` crate)
- Gerber/Excellon export (using `gerber-types`)
- BOM / pick & place export
- **Milestone:** Agent designs a complete 2-layer PCB, exports Gerber files

### Phase 4 — CAD Engine (Weeks 10-12)
Build `volt-cad` as a Rust CLI wrapper around FreeCAD's headless engine.
- FreeCAD Python script generator for enclosure operations
- `freecadcmd` subprocess management and output parsing
- Parametric enclosure generation (box/shell with fillets, chamfers)
- Boolean cutout operations (ports, screen, speakers, grilles)
- PCB mounting (screw bosses, standoffs from board outline)
- PCB 3D import (via KiCad StepUp or STEP)
- Assembly: combine PCB + enclosure + lid
- Interference/fit checking
- STEP and STL export
- **Milestone:** Agent generates a printable enclosure for a PCB

### Phase 5 — Application MVP (Weeks 12-20)
Desktop application tying everything together.
- Tauri app shell
- Agent chat interface
- Schematic viewer (SVG rendering from `volt-eda`)
- PCB viewer (layer-based rendering)
- 3D viewer (wgpu, PCB + enclosure)
- Human-in-the-loop approval gates
- **Milestone:** End-to-end demo — NL prompt to manufacturing files in the GUI

### Phase 6 — Manufacturing & Polish (Weeks 18-24)
Production readiness.
- JLCPCB/PCBWay API integration
- BOM-to-supplier matching
- Cost optimization
- Simulation integration (ngspice)
- Advanced auto-routing
- Multi-board projects
- **Milestone:** User can go from prompt to placed manufacturing order

---

## Competitive Landscape

| | **Volt** | KiCad + Copilot | Flux.ai | Altium 365 |
|---|---|---|---|---|
| NL → Hardware | ✅ End-to-end | ❌ | Partial (schematic only) | ❌ |
| Agent-native | ✅ Built for it | ❌ Bolted on | Partial | ❌ |
| Enclosure design | ✅ Integrated | ❌ | ❌ | ❌ |
| Manufacturing integration | ✅ | ❌ | Partial | ✅ |
| Self-hostable | ✅ | ✅ | ❌ (cloud) | ❌ (cloud) |
| Open-source tools | ✅ MIT | ✅ GPL | ❌ | ❌ |
| Bundleable | ✅ (LGPL CAD) | ❌ GPL | ❌ | ❌ |

---

## LibrePCB File Format Reference

The `volt-eda` native format is LibrePCB's `.lp` S-Expression format. A reference
implementation exists at `/Users/ryanwilliamson/Development/Tools/LibrePCB` and
example project files at `tests/data/projects/Nested Planes/`.

**Project directory structure:**
```
project.lpp                          # Marker file containing "LIBREPCB-PROJECT"
project/
  metadata.lp                        # UUID, name, author, version, created
  settings.lp                        # locale order, norm order, BOM attributes
  jobs.lp                            # output job definitions
circuit/
  circuit.lp                         # netclasses, nets, component instances, signals
  erc.lp                             # ERC message approvals
schematics/
  schematics.lp                      # list of schematic file paths
  <name>/schematic.lp                # symbol placements, net segments, junctions, lines
boards/
  boards.lp                          # list of board file paths
  <name>/board.lp                    # devices, traces, vias, planes, DRC settings
library/
  sym/<uuid>/symbol.lp               # pins, polygons, text
  cmp/<uuid>/component.lp            # signals, gates, variants, pin→signal mapping
  pkg/<uuid>/package.lp              # pads, footprints, courtyard polygons
  dev/<uuid>/device.lp               # component→package binding, pad→signal mapping
```

**S-Expression format example (metadata.lp):**
```sexpr
(librepcb_project_metadata 34ce99d2-d946-43c5-a01b-80a9f9463716
 (name "test_project")
 (author "LibrePCB")
 (version "v1")
 (created 2018-05-05T10:58:49Z)
)
```

**Library hierarchy (4 layers):**
1. **Symbol** — schematic visual representation (pins + polygons + text)
2. **Component** — abstract electrical part (signals, gates that reference symbols, variants for different norms)
3. **Package** — physical footprint (pads with positions/shapes, courtyard polygons)
4. **Device** — binds a Component to a Package (maps each pad to a component signal)

**13 top-level file types:** `librepcb_board`, `librepcb_boards`, `librepcb_circuit`,
`librepcb_component`, `librepcb_device`, `librepcb_erc`, `librepcb_jobs`,
`librepcb_package`, `librepcb_project_metadata`, `librepcb_project_settings`,
`librepcb_schematic`, `librepcb_schematics`, `librepcb_symbol`

**Units:** All lengths are in millimeters with up to 6 decimal places (nanometer precision).
Angles are in degrees. UUIDs are v4 format.

**ERC checks to implement (~10 rules):**
- Unused net classes / buses
- Open nets (single connection point)
- Open wires in net segments
- Unconnected required signals
- Forced net name conflicts
- Unplaced required/optional gates
- Connected pins without wires
- Unconnected junctions

**DRC checks to implement (~30 rules):**
- Missing devices (in circuit but not on board)
- Missing connections (airwires)
- Board outline violations (missing, multiple, open polygons)
- Copper-copper / copper-board / copper-hole clearance
- Drill-drill / drill-board clearance
- Minimum trace width, annular ring, drill diameter, slot width
- Forbidden slots/vias, invalid pad connections
- Device courtyard overlaps
- Keepout zone violations (copper, device, exposure)
- Silkscreen clearance, text height
- Disabled/unused layers

---

## Crate Structure

```
volt-eda/
├── Cargo.toml              (workspace)
├── LICENSE                  (MIT)
├── README.md
├── crates/
│   ├── volt-sexp/          # Generic S-Expression parser/writer
│   ├── volt-core/          # Data model: Circuit, Schematic, Board, Library types
│   ├── volt-librepcb/      # LibrePCB .lp file format serialization/deserialization
│   ├── volt-kicad/         # KiCad .kicad_sym/.kicad_mod library importer
│   ├── volt-erc/           # Electrical rule checking
│   ├── volt-drc/           # Design rule checking (uses `geo` crate)
│   ├── volt-export/        # Gerber, BOM, pick & place, D-356 netlist
│   └── volt-cli/           # CLI binary — the `volt-eda` command
```

---

## Success Metrics

1. **Agent can design a working 2-layer PCB** from a natural language description
   (validated by ERC + DRC pass and Gerber review)
2. **< 5 minutes** from prompt to manufacturing-ready files for simple designs
3. **Open-source adoption** of `volt-eda` and `volt-cad` as standalone tools
4. **First real board manufactured** from an agent-designed project
