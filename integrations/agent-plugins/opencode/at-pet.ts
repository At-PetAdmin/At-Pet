import { Plugin } from "@opencode/plugin"

const apiBase = (process.env.AT_PET_URL || "http://127.0.0.1:8765").replace(/\/+$/, "")
const permissionPollIntervalMs = 500
const requestTimeoutMs = 2000

async function atPetFetch(path: string, init: RequestInit = {}, signal?: AbortSignal) {
  const controller = new AbortController()
  const abortFromParent = () => controller.abort()
  if (signal?.aborted) controller.abort()
  else signal?.addEventListener("abort", abortFromParent, { once: true })
  const timeout = setTimeout(() => controller.abort(), requestTimeoutMs)
  try {
    return await fetch(`${apiBase}${path}`, { ...init, signal: controller.signal })
  } finally {
    clearTimeout(timeout)
    signal?.removeEventListener("abort", abortFromParent)
  }
}

function postEvent(payload: Record<string, unknown>, signal: AbortSignal) {
  return atPetFetch("/agent-event/opencode", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  }, signal).then(() => undefined).catch(() => undefined)
}

function delay(milliseconds: number) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds))
}

async function requestPermission(input: {
  sessionID: string
  action: string
  resources: readonly string[]
}, signal: AbortSignal): Promise<"allow" | "deny" | undefined> {
  let requestId = ""
  try {
    const response = await atPetFetch("/permission", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        sessionId: input.sessionID,
        agentApp: "opencode",
        toolName: input.action,
        title: `OpenCode permission: ${input.action}`,
        description: input.resources.join("\n"),
      }),
    }, signal)
    if (!response.ok) return undefined

    const accepted = await response.json() as { requestId?: unknown }
    if (typeof accepted.requestId !== "string") return undefined
    requestId = accepted.requestId

    while (!signal.aborted) {
      await delay(permissionPollIntervalMs)
      let result: Response
      try {
        result = await atPetFetch(`/permission/${encodeURIComponent(requestId)}`, {}, signal)
      } catch {
        if (signal.aborted) return undefined
        continue
      }
      if (result.status === 404) return undefined
      if (!result.ok) continue

      const decision = await result.json() as { status?: unknown }
      if (decision.status === "allowed_once") return "allow"
      if (decision.status === "denied") return "deny"
    }
  } catch {
    return undefined
  } finally {
    if (requestId) {
      await atPetFetch(`/permission/${encodeURIComponent(requestId)}`, { method: "DELETE" })
        .catch(() => undefined)
    }
  }
}

export default Plugin.define({
  id: "at-pet-agent-monitor",
  setup: async (ctx) => {
    const controller = new AbortController()
    const parents = new Map<string, string>()
    const registrations = [
      await ctx.tool.hook("execute.before", (event) => {
        void postEvent({
          sourceEvent: "tool.execute.before",
          sessionID: event.sessionID,
          toolName: event.tool,
        }, controller.signal)
      }),
      await ctx.tool.hook("execute.after", (event) => {
        void postEvent({
          sourceEvent: "tool.execute.after",
          sessionID: event.sessionID,
          toolName: event.tool,
          statusType: event.status,
          statusMessage: event.status === "error" ? JSON.stringify(event.error) : undefined,
        }, controller.signal)
      }),
      await ctx.permission.hook("evaluate", async (event) => {
        if (event.effect !== "ask") return
        const decision = await requestPermission(event, controller.signal)
        if (decision === "allow") event.effect = "allow"
        if (decision === "deny") {
          event.effect = "deny"
          event.message = "Permission denied in At-Pet."
        }
      }),
    ]

    void (async () => {
      for await (const event of ctx.event.subscribe({ signal: controller.signal })) {
        const data = event.data as Record<string, unknown>
        const sessionID = typeof data.sessionID === "string" ? data.sessionID : ""
        if (!sessionID) continue

        if (event.type === "session.created") {
          const parentID = typeof data.parentID === "string" ? data.parentID : ""
          if (parentID) parents.set(sessionID, parentID)
          void postEvent({
            sourceEvent: "session.created",
            sessionID,
            parentID,
            statusMessage: typeof data.title === "string" ? data.title : "",
          }, controller.signal)
        } else if (event.type === "session.status") {
          const status = data.status as { type?: unknown; message?: unknown } | undefined
          void postEvent({
            sourceEvent: "session.status",
            sessionID,
            parentID: parents.get(sessionID) || "",
            statusType: typeof status?.type === "string" ? status.type : "",
            statusMessage: typeof status?.message === "string" ? status.message : "",
          }, controller.signal)
        } else if (event.type === "session.idle" || event.type === "session.deleted") {
          void postEvent({
            sourceEvent: event.type,
            sessionID,
            parentID: parents.get(sessionID) || "",
          }, controller.signal)
          parents.delete(sessionID)
        }
      }
    })().catch(() => undefined)

    return async () => {
      controller.abort()
      await Promise.all(registrations.map((registration) => registration.dispose()))
    }
  },
})
