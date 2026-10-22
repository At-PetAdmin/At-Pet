param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('userPromptSubmitted', 'preToolUse', 'postToolUse', 'postToolUseFailure', 'agentStop', 'sessionEnd')]
    [string]$HookEvent
)

$ErrorActionPreference = 'Stop'

try {
    $hookInput = [Console]::In.ReadToEnd()
    if ([string]::IsNullOrWhiteSpace($hookInput)) {
        exit 0
    }

    $hook = ConvertFrom-Json -InputObject $hookInput -ErrorAction Stop
    $sessionId = [string]$hook.sessionId
    if ([string]::IsNullOrWhiteSpace($sessionId)) {
        exit 0
    }

    $eventName = switch ($HookEvent) {
        'userPromptSubmitted' { 'thinking' }
        'preToolUse' { 'tool_start' }
        'postToolUse' { 'tool_end' }
        'postToolUseFailure' { 'error' }
        'agentStop' { 'completed' }
        'sessionEnd' { 'session_end' }
    }

    $message = ''
    if ($HookEvent -in @('preToolUse', 'postToolUse', 'postToolUseFailure')) {
        $message = [string]$hook.toolName
    }

    $eventUrl = $env:AT_PET_EVENT_URL
    if ([string]::IsNullOrWhiteSpace($eventUrl)) {
        $eventUrl = 'http://127.0.0.1:8765/event'
    }

    $endpoint = [Uri]$eventUrl
    if ($endpoint.Scheme -ne 'http' -or -not $endpoint.IsLoopback) {
        exit 0
    }

    $body = [ordered]@{
        sessionId       = $sessionId
        agentApp        = 'github-copilot'
        event           = $eventName
        message         = $message
        isSubagent      = $false
        parentSessionId = ''
    } | ConvertTo-Json -Compress

    Invoke-RestMethod -Uri $endpoint -Method Post -ContentType 'application/json; charset=utf-8' -Body $body -TimeoutSec 2 -ErrorAction Stop | Out-Null
}
catch {
}

exit 0
