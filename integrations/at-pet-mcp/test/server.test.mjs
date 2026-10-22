import assert from "node:assert/strict";
import { createServer } from "node:http";
import { once } from "node:events";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";
import test from "node:test";

const serverPath = fileURLToPath(new URL("../server.mjs", import.meta.url));

test("exposes MCP tools and forwards a limited event to At-Pet", async () => {
  const receivedEvents = [];
  const api = createServer(async (request, response) => {
    if (request.method === "GET" && request.url === "/health") {
      response.writeHead(200, { "content-type": "application/json" });
      response.end(JSON.stringify({ ok: true }));
      return;
    }

    if (request.method === "POST" && request.url === "/event") {
      const chunks = [];
      for await (const chunk of request) chunks.push(chunk);
      receivedEvents.push(JSON.parse(Buffer.concat(chunks).toString("utf8")));
      response.writeHead(202, { "content-type": "application/json" });
      response.end(JSON.stringify({ accepted: true }));
      return;
    }

    response.writeHead(404);
    response.end();
  });

  api.listen(0, "127.0.0.1");
  await once(api, "listening");
  const { port } = api.address();
  const transport = new StdioClientTransport({
    command: process.execPath,
    args: [serverPath],
    env: {
      ...process.env,
      AT_PET_URL: `http://127.0.0.1:${port}`,
      AT_PET_AGENT_APP: "opencode"
    }
  });
  const client = new Client({ name: "at-pet-mcp-test", version: "1.0.0" });

  try {
    await client.connect(transport);
    const { tools } = await client.listTools();
    assert.deepEqual(
      tools.map((tool) => tool.name).sort(),
      ["check_at_pet_connection", "report_agent_event"].sort()
    );

    const health = await client.callTool({
      name: "check_at_pet_connection",
      arguments: {}
    });
    assert.equal(health.isError, undefined);
    assert.match(health.content[0].text, /At-Pet is available/);

    const report = await client.callTool({
      name: "report_agent_event",
      arguments: {
        event: "tool_start",
        sessionId: "test-session",
        toolName: "Read"
      }
    });
    assert.equal(report.isError, undefined);
    assert.match(report.content[0].text, /accepted tool_start/);
    assert.deepEqual(receivedEvents, [
      {
        sessionId: "test-session",
        agentApp: "opencode",
        event: "tool_start",
        toolName: "Read",
        message: "Read"
      }
    ]);
  } finally {
    await client.close();
    await new Promise((resolve) => api.close(resolve));
  }
});

test("rejects non-loopback At-Pet URLs", async () => {
  const child = spawn(process.execPath, [serverPath], {
    env: { ...process.env, AT_PET_URL: "http://example.com" },
    stdio: ["ignore", "ignore", "pipe"]
  });
  let stderr = "";
  child.stderr.setEncoding("utf8").on("data", (chunk) => {
    stderr += chunk;
  });
  const [code] = await once(child, "exit");
  assert.equal(code, 1);
  assert.match(stderr, /must be an HTTP URL on loopback/);
});
