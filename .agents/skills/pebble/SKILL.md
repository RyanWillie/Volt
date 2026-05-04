---
name: pebble
description: Use when managing issues, tasks, bugs, or dependencies in this project. This project uses Pebble (pb) for issue tracking.
---

# Pebble Issue Tracker

This project uses **Pebble** (`pb`) for issue tracking. All data lives in a global SQLite database at `~/.pebble/pebble.db`. Issues are scoped to projects — by default, commands only show issues for the current project.

## Quick Reference

| Command | Description |
|---------|-------------|
| `pb create <title>` | Create a new issue |
| `pb list` | List open issues (current project) |
| `pb list --global` | List open issues across all projects |
| `pb show <id>` | Show issue details, history, and dependencies |
| `pb update <id>` | Update issue fields |
| `pb close <id>` | Close an issue |
| `pb search <query>` | Full-text search across issues |
| `pb dep add <src> <tgt>` | Add a dependency between issues |
| `pb label add <id> <label>` | Add a label to an issue |
| `pb ready` | List open issues with no unresolved blockers |
| `pb tree` | Display issues as a tree hierarchy |
| `pb projects` | List all registered projects |

All commands accept `--json` for machine-readable output.

Issue IDs are 8-character hex strings. You can use any unique prefix to reference an issue (like git short hashes).

## Project Scoping

By default, all commands scope to the current project (auto-detected via git). Use these flags to change scope:

- `-g, --global` — Show issues across all projects
- `--project <name>` — Target a specific project by name or ID

Projects are registered with `pb init` and identified by their git repository root. **Worktrees** of the same repository share the same project.

## Creating Issues

```bash
# Minimal
pb create "Fix login timeout"

# With all options
pb create "Add dark mode" \
  -d "Users want a dark theme option" \
  -p high \
  -t feature \
  -a alice \
  -l ui -l enhancement \
  --parent <epic-id>
```

**Options:**
- `-p, --priority`: critical | high | medium | low | none (default: medium)
- `-t, --type`: task | bug | feature | epic (default: task)
- `-a, --assignee`: assign to someone
- `-l, --label`: add labels (repeatable)
- `--parent`: set parent issue for hierarchy
- `-d, --desc`: description text

## Listing and Filtering

```bash
# All open issues (default, current project)
pb list

# All open issues across all projects
pb list --global

# Filter by status, priority, type, assignee, or label
pb list -s in_progress
pb list -p critical
pb list -t bug
pb list -a alice
pb list -l urgent

# Include closed issues
pb list --all

# Limit results
pb list -n 10
```

Issues are sorted by priority (critical first), then by most recently updated.

## Updating Issues

```bash
pb update <id> --title "New title"
pb update <id> -s in_progress
pb update <id> -p critical
pb update <id> -a bob
```

All changes are recorded in the issue's event history.

## Dependencies

```bash
# A blocks B (A must be done before B)
pb dep add <A> <B>

# Related issues (informational)
pb dep add <A> <B> -k related

# Remove a dependency
pb dep remove <A> <B>

# List dependencies for an issue
pb dep list <id>
```

Pebble prevents circular blocking dependencies automatically.

## Workflow Guidance

When working on this project:

1. **Before starting work**, run `pb ready` to find unblocked issues sorted by priority.
2. **When starting an issue**, update its status: `pb update <id> -s in_progress`
3. **When you discover a bug** or new task, create an issue: `pb create "description" -t bug`
4. **When finished**, close the issue: `pb close <id> -r "reason"`
5. **Use `pb search`** to check if a related issue already exists before creating duplicates.
6. **Use `--json` flag** when you need to parse output programmatically.
