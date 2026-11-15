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

    $apiToken = $env:AT_PET_API_TOKEN_GITHUBCOPILOT
    if ([string]::IsNullOrWhiteSpace($apiToken)) { $apiToken = $env:AT_PET_API_TOKEN }
    if ([string]::IsNullOrWhiteSpace($apiToken)) {
        $credentialFile = $env:AT_PET_CREDENTIALS
        if ([string]::IsNullOrWhiteSpace($credentialFile)) {
            $credentialFile = Join-Path $env:LOCALAPPDATA 'Ni-ShuWu/At-Pet/api-security/clients.json'
        }
        $credentialStore = Get-Content -LiteralPath $credentialFile -Raw | ConvertFrom-Json
        if ($credentialStore.version -ne 1) { exit 0 }
        $apiToken = [string]($credentialStore.clients | Where-Object {
            $_.id -eq 'githubcopilot' -and $_.agentKey -eq 'githubcopilot' -and -not $_.administrator
        } | Select-Object -First 1).token
    }
    if ([string]::IsNullOrWhiteSpace($apiToken)) { exit 0 }
    $eventBody = [ordered]@{
        sessionId       = $sessionId
        agentApp        = 'github-copilot'
        event           = $eventName
        message         = $message
        isSubagent      = $false
        parentSessionId = ''
        eventId         = [Guid]::NewGuid().ToString('D')
        occurredAt      = [DateTime]::UtcNow.ToString('o')
        turnStart       = ($HookEvent -eq 'userPromptSubmitted')
    }
    if ($hook.toolCallId) { $eventBody.toolCallId = [string]$hook.toolCallId }
    $body = $eventBody | ConvertTo-Json -Compress

    Invoke-RestMethod -Uri $endpoint -Method Post -Headers @{ Authorization = "Bearer $apiToken" } -MaximumRedirection 0 -ContentType 'application/json; charset=utf-8' -Body $body -TimeoutSec 2 -ErrorAction Stop | Out-Null
}
catch {
}

exit 0
