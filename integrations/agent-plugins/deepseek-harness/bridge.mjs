#!/usr/bin/env node
// At-Pet bridge: turns agent hook payloads into At-Pet events and relays permission prompts.
// Usage: node bridge.mjs [agentApp] [hookEvent]   stdin: hook payload JSON
import { createHash } from "node:crypto";

const AGENT_APP = process.argv[2] || "deepseek-harness";
const EVENT_NAME = process.argv[3] || "";
const BASE_URL = (process.env.AT_PET_URL || "http://127.0.0.1:8765").replace(/\/+$/, "");
const REQUEST_TIMEOUT_MS = 2000;
const POLL_INTERVAL_MS = 500;
const PERMISSION_WAIT_MS = 590_000;

const HOOK_NAMES = {
  sessionstart: "SessionStart",
  userpromptsubmit: "UserPromptSubmit",
  userpromptsubmitted: "UserPromptSubmit",
  userprompt: "UserPromptSubmit",
  pretooluse: "PreToolUse",
  posttooluse: "PostToolUse",
  posttoolusefailure: "PostToolUseFailure",
  erroroccurred: "ErrorOccurred",
  permissionrequest: "PermissionRequest",
  notification: "Notification",
  stop: "Stop",
  agentstop: "Stop",
  sessionend: "SessionEnd",
  subagentstart: "SubagentStart",
  subagentstop: "SubagentStop",
};

const EVENT_FOR_HOOK = {
  UserPromptSubmit: "thinking",
  PreToolUse: "tool_start",
  PostToolUse: "tool_end",
  PostToolUseFailure: "error",
  ErrorOccurred: "error",
  Stop: "done",
  SessionEnd: "session_end",
  SubagentStart: "thinking",
  SubagentStop: "done",
};

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

function readStdin() {
  return new Promise((resolve) => {
    let data = "";
    process.stdin.setEncoding("utf8");
    process.stdin.on("data", (chunk) => (data += chunk));
    process.stdin.on("end", () => resolve(data));
    process.stdin.on("error", () => resolve(""));
  });
}

function sessionOf(payload) {
  const id = payload.session_id ?? payload.sessionId;
  if (typeof id === "string" && id) return id;
  const cwd = typeof payload.cwd === "string" ? payload.cwd : process.cwd();
  return `${AGENT_APP}-${createHash("sha1").update(cwd).digest("hex").slice(0, 12)}`;
}

function request(path, init = {}) {
  return fetch(`${BASE_URL}${path}`, { ...init, signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS) });
}

function postJson(path, body) {
  return request(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
}

async function askPermission(payload, sessionId) {
  const toolName = String(payload.tool_name ?? payload.toolName ?? "tool");
  const detail = payload.tool_input ?? {};
  const description = [detail.description, detail.command]
    .filter((value) => typeof value === "string" && value)
    .join("\n");
  let requestId = "";
  try {
    const accepted = await postJson("/permission", {
      sessionId,
      agentApp: AGENT_APP,
      toolName,
      title: `${AGENT_APP} permission: ${toolName}`,
      description,
    });
    if (!accepted.ok) return null;
    requestId = String((await accepted.json()).requestId ?? "");
    if (!requestId) return null;
    const deadline = Date.now() + PERMISSION_WAIT_MS;
    while (Date.now() < deadline) {
      await sleep(POLL_INTERVAL_MS);
      const result = await request(`/permission/${encodeURIComponent(requestId)}`);
      if (result.status === 404) return null;
      if (!result.ok) continue;
      const { status } = await result.json();
      if (status === "allowed_once") return "allow";
      if (status === "denied") return "deny";
    }
  } catch {
    // At-Pet is unavailable: leave the agent's own approval prompt in place.
  }
  if (requestId) {
    await request(`/permission/${encodeURIComponent(requestId)}`, { method: "DELETE" }).catch(() => {});
  }
  return null;
}

async function main() {
  const payload = JSON.parse((await readStdin()) || "{}");
  const key = (EVENT_NAME || payload.hook_event_name || payload.hook || "").toLowerCase().replace(/[^a-z]/g, "");
  const hookName = HOOK_NAMES[key];
  if (!hookName) return;

  const sessionId = sessionOf(payload);
  if (hookName === "PermissionRequest") {
    const decision = await askPermission(payload, sessionId);
    if (decision) {
      const message = decision === "deny" ? { message: "Denied in At-Pet." } : {};
      process.stdout.write(
        JSON.stringify({
          hookSpecificOutput: {
            hookEventName: "PermissionRequest",
            decision: { behavior: decision, ...message },
          },
        }),
      );
    }
    return;
  }

  let event = EVENT_FOR_HOOK[hookName];
  if (hookName === "Notification" && payload.notification_type === "permission_prompt") event = "permission";
  if (!event) return;

  const isSubagent = hookName.startsWith("Subagent");
  const body = {
    sessionId: isSubagent && payload.agent_id ? String(payload.agent_id) : sessionId,
    agentApp: AGENT_APP,
    event,
    model: typeof payload.model === "string" ? payload.model : "",
    isSubagent,
    parentSessionId: isSubagent ? sessionId : "",
  };
  const toolName = payload.tool_name ?? payload.toolName;
  if (toolName) body.message = String(toolName);
  await postJson("/event", body);
}

main()
  .catch(() => {})
  .finally(() => {
    process.exitCode = 0;
  });
