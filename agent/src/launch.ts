/**
 * Volt Agent Launcher
 *
 * Launches a Pi agent session configured for hardware design.
 * The agent is sandboxed to the project directory, with Volt's bundled
 * skills, extensions, and system prompt.
 *
 * Usage:
 *   npx tsx src/launch.ts /path/to/project           # Interactive mode
 *   npx tsx src/launch.ts /path/to/project "prompt"   # Single-shot mode
 *
 * The project directory must be a valid volt-eda project (contains volt.json),
 * or the agent will create one.
 */

import { resolve, join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import {
  AuthStorage,
  createAgentSession,
  createAgentSessionRuntime,
  createAgentSessionFromServices,
  createAgentSessionServices,
  createSyntheticSourceInfo,
  DefaultResourceLoader,
  getAgentDir,
  InteractiveMode,
  ModelRegistry,
  SessionManager,
  type Skill,
  type CreateAgentSessionRuntimeFactory,
} from "@mariozechner/pi-coding-agent";

// ---------------------------------------------------------------------------
// Resolve paths
// ---------------------------------------------------------------------------

const __dirname = dirname(fileURLToPath(import.meta.url));
const agentRoot = resolve(__dirname, "..");

const args = process.argv.slice(2);
const projectDir = args[0] ? resolve(args[0]) : process.cwd();
const initialPrompt = args[1] || undefined;

// ---------------------------------------------------------------------------
// Build skill definitions from agent/skills/
// ---------------------------------------------------------------------------

function voltSkills(): Skill[] {
  const skillDefs = [
    {
      name: "eda-operator",
      description:
        "Drive the volt-eda CLI to create and modify hardware projects. Use when building circuits, schematics, PCB layouts, running DRC, or exporting manufacturing files.",
    },
    {
      name: "design-knowledge",
      description:
        "Hardware design reference knowledge including component selection rules, common circuit patterns, PCB layout guidelines, and electrical design best practices.",
    },
    {
      name: "hardware-designer",
      description:
        "Design complete hardware products from natural language descriptions. Decomposes requirements, selects components, builds circuits, creates schematics, lays out PCBs, validates with DRC, and exports manufacturing files.",
    },
  ];

  return skillDefs.map((s) => {
    const skillDir = join(agentRoot, "skills", s.name);
    const skillPath = join(skillDir, "SKILL.md");
    return {
      name: s.name,
      description: s.description,
      filePath: skillPath,
      baseDir: skillDir,
      sourceInfo: createSyntheticSourceInfo(skillPath, { source: "volt" }),
      disableModelInvocation: false,
    };
  });
}

// ---------------------------------------------------------------------------
// System prompt
// ---------------------------------------------------------------------------

function voltSystemPrompt(): string {
  return `You are a Volt hardware design agent. You design electronic hardware products from natural language descriptions, producing manufacturing-ready PCB designs.

You have access to \`volt-eda\`, a CLI tool for all EDA operations. Every command produces structured JSON output. The current working directory is the user's project.

You are sandboxed to the project directory. Do not attempt to access files outside it.

Available skills (load with the read tool when needed):
- hardware-designer: Top-level orchestration for full product design
- eda-operator: Complete volt-eda CLI reference with all commands
- design-knowledge: Reference circuits, component selection, PCB layout rules

When the user describes a hardware product, load the hardware-designer skill and follow its process. For specific volt-eda commands, load eda-operator. For circuit design decisions, load design-knowledge.

Always:
- Parse JSON output to check for errors
- Run ERC after wiring circuits
- Run DRC after laying out boards
- Render visuals for verification
- Export all manufacturing files when complete`;
}

// ---------------------------------------------------------------------------
// Launch
// ---------------------------------------------------------------------------

async function main() {
  console.log(`🔌 Volt Hardware Design Agent`);
  console.log(`📁 Project: ${projectDir}`);
  console.log(`🔧 Agent:   ${agentRoot}`);
  console.log();

  const authStorage = AuthStorage.create();
  const modelRegistry = ModelRegistry.create(authStorage);

  const sandboxExtensionPath = join(agentRoot, "extensions", "volt-sandbox.ts");

  const createRuntime: CreateAgentSessionRuntimeFactory = async ({
    cwd,
    sessionManager,
    sessionStartEvent,
  }) => {
    const services = await createAgentSessionServices({ cwd });

    // Override resource loading with Volt's bundled resources
    const loader = new DefaultResourceLoader({
      cwd,
      agentDir: getAgentDir(),
      systemPromptOverride: voltSystemPrompt,
      additionalExtensionPaths: [sandboxExtensionPath],
      skillsOverride: (current) => ({
        // Keep any user-defined skills from the project, add Volt's skills
        skills: [...current.skills, ...voltSkills()],
        diagnostics: current.diagnostics,
      }),
      agentsFilesOverride: (current) => ({
        agentsFiles: [
          ...current.agentsFiles,
          {
            path: join(agentRoot, "AGENTS.md"),
            content: `# Volt Hardware Design Agent

You are a Volt hardware design agent. You design electronic hardware products from natural language descriptions, producing manufacturing-ready PCB files.

## Tools

You have access to \`volt-eda\`, a CLI tool in the PATH that handles all EDA operations. Every command produces JSON output. The project directory is your current working directory.

## Constraints

- You are sandboxed to this project directory. Do not attempt to access files outside it.
- All designs use the volt-eda JSON project format.
- Focus on producing correct, manufacturing-ready output.
- Run ERC and DRC validation after building the circuit and board respectively.
- Render schematics and boards for visual verification.`,
          },
        ],
      }),
    });
    await loader.reload();

    return {
      ...(await createAgentSessionFromServices({
        services: { ...services, resourceLoader: loader },
        sessionManager,
        sessionStartEvent,
      })),
      services,
      diagnostics: services.diagnostics,
    };
  };

  const runtime = await createAgentSessionRuntime(createRuntime, {
    cwd: projectDir,
    agentDir: getAgentDir(),
    sessionManager: SessionManager.create(projectDir),
  });

  if (initialPrompt) {
    // Single-shot mode: send prompt, print output, exit
    const session = runtime.session;
    session.subscribe((event) => {
      if (
        event.type === "message_update" &&
        event.assistantMessageEvent.type === "text_delta"
      ) {
        process.stdout.write(event.assistantMessageEvent.delta);
      }
    });

    await session.prompt(initialPrompt);
    console.log();
    session.dispose();
  } else {
    // Interactive mode
    const mode = new InteractiveMode(runtime, {
      migratedProviders: [],
      modelFallbackMessage: undefined,
      initialMessage: undefined,
      initialImages: [],
      initialMessages: [],
    });
    await mode.run();
  }
}

main().catch((err) => {
  console.error("Fatal:", err);
  process.exit(1);
});
