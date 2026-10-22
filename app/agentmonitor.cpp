#include "agentmonitor.h"

#include "agentadapters/claude-code/adapter.h"
#include "agentadapters/opencode-v2/adapter.h"

const AgentMonitorStrategy *agentMonitorStrategy(AgentMonitorType type) {
    switch (type) {
    case AgentMonitorType::ClaudeCodeHttpHooks:
        return agentadapters::claudeCodeHttpHooksStrategy();
    case AgentMonitorType::OpenCodeV2Plugin:
        return agentadapters::openCodeV2PluginStrategy();
    case AgentMonitorType::ExternalHttpApi:
        return nullptr;
    }
    return nullptr;
}
