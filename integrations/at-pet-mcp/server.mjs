import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";

const DEFAULT_API_URL = "http://127.0.0.1:8765";
const ALLOWED_HOSTS = new Set(["127.0.0.1", "localhost", "::1", "[::1]"]);

function resolveApiOrigin(value) {
  const url = new URL(value || DEFAULT_API_URL);
  if (
    url.protocol !== "http:" ||
    !ALLOWED_HOSTS.has(url.hostname) ||
    url.username ||
    url.password ||
    url.pathname !== "/" ||
    url.search ||
    url.hash
  ) {
    throw new Error(
      "AT_PET_URL must be an HTTP URL on loopback (127.0.0.1, localhost, or ::1), without a path."
    );
  }
  return url.origin;
}

let apiOrigin;
try {
  apiOrigin = resolveApiOrigin(process.env.AT_PET_URL);
} catch (error) {
  console.error(`[at-pet-mcp] ${error.message}`);
  process.exit(1);
}

const defaultAgentApp = process.env.AT_PET_AGENT_APP?.trim() || "external";
const server = new McpServer({ name: "at-pet-mcp", version: "0.1.0" });

function toolError(message) {
  return { isError: true, content: [{ type: "text", text: message }] };
}

server.registerTool(
  "check_at_pet_connection",
  {
    title: "Check At-Pet connection",
    description: "Check whether the local At-Pet HTTP API is responding.",
    inputSchema: {}
  },
  async () => {
    try {
      const response = await fetch(`${apiOrigin}/health`, {
        signal: AbortSignal.timeout(3000)
      });
      if (!response.ok) {
        return toolError(`At-Pet health check returned HTTP ${response.status}.`);
      }
      const health = await response.json();
      if (health.ok !== true) {
        return toolError("At-Pet returned an unexpected health response.");
      }
      return { content: [{ type: "text", text: "At-Pet is available." }] };
    } catch (error) {
      return toolError(`Cannot reach At-Pet at ${apiOrigin}: ${error.message}`);
    }
  }
);

server.registerTool(
  "report_agent_event",
  {
    title: "Report an Agent event to At-Pet",
    description:
      "Report one lifecycle event to At-Pet. Reuse the same sessionId for every event in the current Agent conversation. This tool only sends the event you explicitly provide; it does not monitor the Agent automatically.",
    inputSchema: {
      event: z
        .enum([
          "started",
          "thinking",
          "work",
          "review",
          "tool_start",
          "tool_end",
          "completed",
          "cancelled",
          "error",
          "idle",
          "session_end"
        ])
        .describe("Lifecycle event to report."),
      sessionId: z
        .string()
        .trim()
        .min(1)
        .max(256)
        .describe("Stable identifier for this Agent conversation."),
      agentApp: z
        .string()
        .trim()
        .min(1)
        .max(64)
        .optional()
        .describe("Optional source label; defaults to AT_PET_AGENT_APP or external."),
      toolName: z
        .string()
        .trim()
        .min(1)
        .max(160)
        .optional()
        .describe("Optional tool name for tool_start or tool_end. Do not include tool arguments or results.")
    }
  },
  async ({ event, sessionId, agentApp, toolName }) => {
    const source = agentApp || defaultAgentApp;
    const payload = { sessionId, agentApp: source, event };
    if (toolName) {
      payload.toolName = toolName;
      if (event === "tool_start") payload.message = toolName;
    }

    try {
      const response = await fetch(`${apiOrigin}/event`, {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify(payload),
        signal: AbortSignal.timeout(3000)
      });
      if (!response.ok) {
        return toolError(`At-Pet event endpoint returned HTTP ${response.status}.`);
      }
      return {
        content: [
          {
            type: "text",
            text: `At-Pet accepted ${event} for ${source} (session ${sessionId}).`
          }
        ]
      };
    } catch (error) {
      return toolError(`Cannot send the event to At-Pet at ${apiOrigin}: ${error.message}`);
    }
  }
);

const transport = new StdioServerTransport();
server.connect(transport).catch((error) => {
  console.error(`[at-pet-mcp] ${error.message}`);
  process.exitCode = 1;
});
