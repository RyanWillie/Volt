# Volt Agent Harness

Pi-based agent runtime for autonomous hardware design.

## Architecture

The agent harness is a **Pi configuration layer** — not a custom runtime. It uses Pi's SDK to launch a sandboxed agent session with Volt-specific skills, extensions, and system prompt.

```
┌─────────────────────────────────────────────┐
│  Volt Application                           │
│                                             │
│  agent/                                     │
│  ├── extensions/volt-sandbox.ts  ← sandbox  │
│  ├── skills/                                │
│  │   ├── eda-operator/          ← CLI ref   │
│  │   ├── design-knowledge/      ← circuits  │
│  │   └── hardware-designer/     ← orchestr  │
│  ├── AGENTS.md                  ← context   │
│  └── src/launch.ts              ← launcher  │
│                                             │
│  Pi SDK (createAgentSession)                │
│  ├── cwd → user's project directory         │
│  ├── tools → read, write, edit, bash        │
│  ├── sandbox → project dir + /tmp only      │
│  └── skills → bundled + user's own          │
└─────────────────────────────────────────────┘
         │
         ▼
┌─────────────────────┐
│  User's Project     │  (clean — no agent files)
│  ├── volt.json      │
│  ├── circuit.json   │
│  ├── schematics/    │
│  ├── boards/        │
│  ├── library/       │
│  └── .agents/       │  (optional user skills)
│      └── skills/    │
└─────────────────────┘
```

## Sandbox

The agent is confined to the project directory:

- **File operations** (read/write/edit): only within project dir and `/tmp`
- **Bash commands**: `volt-eda` and basic file operations allowed
- **Blocked**: `ssh`, `sudo`, access to `~/.ssh`, `~/.aws`, `~/.gnupg`

## Skills

| Skill | Purpose |
|-------|---------|
| `hardware-designer` | Top-level orchestration: NL → decompose → build → validate → export |
| `eda-operator` | Complete volt-eda CLI reference with every command and example |
| `design-knowledge` | Reference circuits, component selection, PCB layout guidelines |

## Usage

```bash
# Install dependencies
cd agent && npm install

# Launch interactive mode (pointed at a project)
npx tsx src/launch.ts /path/to/project

# Single-shot mode
npx tsx src/launch.ts /path/to/project "Design a USB-C powered LED blinker with an ATtiny85"
```

The Volt desktop application will call `launch.ts` programmatically via the SDK.

## User Customization

Users can add their own skills to `.agents/skills/` in their project directory. These are discovered automatically and available alongside the bundled Volt skills. Example uses:

- Company-specific component preferences
- Custom design rules
- Preferred suppliers and part numbers
