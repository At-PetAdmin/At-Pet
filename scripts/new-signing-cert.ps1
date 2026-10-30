# Creates a self-signed Authenticode code-signing certificate for the At-Pet CI workflows.
#
# The script writes a password-protected .pfx plus its public .cer and prints the values for the
# WINDOWS_CERT_PFX_BASE64 and WINDOWS_CERT_PASSWORD repository secrets. Its output contains the
# .pfx password, so treat the console transcript as a secret: do not paste it into commits,
# issues, or CI logs.
#
# Usage:
#   pwsh -File scripts/new-signing-cert.ps1
#   pwsh -File scripts/new-signing-cert.ps1 -OutputDirectory D:\signing -ValidYears 5

[CmdletBinding()]
param(
    [string]$OutputDirectory = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'At-Pet-signing'),
    [int]$ValidYears = 3,
    [string]$Subject = 'CN=At-Pet Self-Signed, O=At-Pet, C=CN',
    [string]$Password
)

$ErrorActionPreference = 'Stop'

if ($PSVersionTable.PSVersion.Major -ge 6 -and -not $IsWindows) {
    throw 'New-SelfSignedCertificate is only available on Windows.'
}
if ($ValidYears -lt 1) {
    throw '-ValidYears must be at least 1.'
}

Import-Module PKI -ErrorAction Stop

if (-not $Password) {
    $alphabet = 'ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789'
    $bytes = New-Object byte[] 40
    [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
    $Password = -join ($bytes | ForEach-Object { $alphabet[$_ % $alphabet.Length] })
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$pfxPath = Join-Path $OutputDirectory 'at-pet-codesign.pfx'
$cerPath = Join-Path $OutputDirectory 'at-pet-codesign.cer'
$base64Path = Join-Path $OutputDirectory 'at-pet-codesign.pfx.base64'

$cert = New-SelfSignedCertificate `
    -Type CodeSigningCert `
    -Subject $Subject `
    -FriendlyName 'At-Pet code signing (self-signed)' `
    -KeyAlgorithm RSA `
    -KeyLength 3072 `
    -HashAlgorithm SHA256 `
    -KeyExportPolicy Exportable `
    -CertStoreLocation Cert:\CurrentUser\My `
    -NotAfter (Get-Date).AddYears($ValidYears)

$securePassword = ConvertTo-SecureString $Password -AsPlainText -Force
Export-PfxCertificate -Cert $cert -FilePath $pfxPath -Password $securePassword | Out-Null
Export-Certificate -Cert $cert -FilePath $cerPath | Out-Null
$base64 = [Convert]::ToBase64String([IO.File]::ReadAllBytes($pfxPath))
[IO.File]::WriteAllText($base64Path, $base64)

Write-Host ''
Write-Host "Certificate : $($cert.Subject)"
Write-Host "Thumbprint  : $($cert.Thumbprint)"
Write-Host "Valid until : $($cert.NotAfter.ToString('yyyy-MM-dd'))"
Write-Host "PFX         : $pfxPath"
Write-Host "Public cert : $cerPath"
Write-Host "Base64      : $base64Path"
Write-Host ''
Write-Host 'Configure the repository secrets (the password below is a secret):'
Write-Host ''
Write-Host "  gh secret set WINDOWS_CERT_PFX_BASE64 --repo At-PetAdmin/At-Pet --body (Get-Content -Raw '$base64Path')"
Write-Host "  gh secret set WINDOWS_CERT_PASSWORD --repo At-PetAdmin/At-Pet --body '$Password'"
Write-Host ''
Write-Host 'Keep the .pfx and its password somewhere safe: they cannot be recovered, and a new'
Write-Host 'certificate changes the signer thumbprint seen by users and SmartScreen.'
