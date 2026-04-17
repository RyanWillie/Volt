/**
 * Volt Project Sandbox Extension
 *
 * Confines the agent to the project directory. All file operations (read, write,
 * edit, bash) are validated to ensure they don't escape the project root.
 *
 * This extension is loaded by the Volt application when launching an agent
 * session. The project root is passed as cwd to the Pi session.
 *
 * Rules:
 * - read/write/edit: project root only, plus read-only access to bundled agent resources
 * - bash: commands are allowed only when they stay within the project workflow
 * - No access to /tmp for project creation or output
 * - No access to home directory, ~/.ssh, ~/.aws, etc.
 */

import { resolve, relative, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import type { ExtensionAPI } from "@mariozechner/pi-coding-agent";

// The agent root is the parent of the extensions/ directory
const __dirname = dirname(fileURLToPath(import.meta.url));
const agentRoot = resolve(__dirname, "..");

export default function (pi: ExtensionAPI) {
  let projectRoot = "";

  pi.on("session_start", async (_event, ctx) => {
    projectRoot = ctx.cwd;
    ctx.ui.setStatus(
      "sandbox",
      ctx.ui.theme.fg("accent", `🔒 Volt sandbox: ${projectRoot}`)
    );
  });

  pi.on("tool_call", async (event, ctx) => {
    const { toolName, input } = event;

    // Validate file path operations
    if (toolName === "read" || toolName === "write" || toolName === "edit") {
      const filePath = input.path as string | undefined;
      if (!filePath) return undefined;

      const resolved = resolve(projectRoot, filePath);
      const rel = relative(projectRoot, resolved);

      // Allow paths within project root
      if (!rel.startsWith("..") && !resolve(rel).startsWith("/")) {
        return undefined;
      }

      // Allow read-only access to agent's bundled skills/resources
      if (toolName === "read") {
        const relToAgent = relative(agentRoot, resolved);
        if (!relToAgent.startsWith("..")) {
          return undefined;
        }
      }

      // Block everything else
      if (ctx.hasUI) {
        ctx.ui.notify(
          `🚫 Blocked ${toolName} outside project: ${filePath}`,
          "warning"
        );
      }
      return {
        block: true,
        reason: `Path "${filePath}" resolves to "${resolved}" which is outside the project directory "${projectRoot}". The agent can only access files within the project directory, plus read-only bundled agent resources.`,
      };
    }

    // For bash, validate no obvious escapes
    if (toolName === "bash") {
      const command = (input.command as string) || "";

      // Block common escape patterns
      const blocked = [
        /\bssh\b/,
        /\bcurl\b.*\|.*\bsh\b/,
        /\bwget\b.*\|.*\bsh\b/,
        /\brm\s+-rf\s+\//,
        /\bsudo\b/,
        /~\/\.ssh/,
        /~\/\.aws/,
        /~\/\.gnupg/,
        /(^|\s)cd\s+\/tmp(\/|\s|$)/,
        /(^|\s)cd\s+\/(\s|$)/,
        /volt-eda\b.*--project\s+\/tmp(\/|\s|$)/,
        /volt-eda\b.*--output(?:-dir)?\s+\/tmp(\/|\s|$)/,
        /volt-eda\b.*\bnew\b.*\/tmp\//,
      ];

      for (const pattern of blocked) {
        if (pattern.test(command)) {
          if (ctx.hasUI) {
            ctx.ui.notify(`🚫 Blocked dangerous command pattern`, "warning");
          }
          return {
            block: true,
            reason: `Command blocked by Volt sandbox: matches restricted pattern. The agent should only use volt-eda commands and basic file operations within the project directory.`,
          };
        }
      }
    }

    return undefined;
  });

  // Register /sandbox command to show current config
  pi.registerCommand("sandbox", {
    description: "Show Volt sandbox configuration",
    handler: async (_args, ctx) => {
      ctx.ui.notify(
        [
          "Volt Project Sandbox",
          "",
          `Project root: ${projectRoot}`,
          "",
          "Allowed:",
          `  • Read/write/edit within: ${projectRoot}`,
          `  • Read-only access to bundled skills: ${agentRoot}`,
          "  • volt-eda CLI commands that operate on the current project",
          "",
          "Blocked:",
          "  • File access outside project root",
          "  • Using /tmp as a project location or output destination",
          "  • ssh, sudo, dangerous shell patterns",
          "  • Home directory sensitive files (~/.ssh, ~/.aws)",
        ].join("\n"),
        "info"
      );
    },
  });
}
