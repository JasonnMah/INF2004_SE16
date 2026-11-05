# Start the temperature-log receiver using the normal demonstration settings.
# Paths derive from this script, so the default output does not depend on cwd.
[CmdletBinding()]
param(
    [string]$Bind = '0.0.0.0',
    [ValidateRange(1,65535)][int]$Port = 8000,
    [string]$Output = (Join-Path (Split-Path $PSScriptRoot -Parent) 'http_received\pc_interval'),
    [switch]$DropFirstReceipt
)
$ErrorActionPreference = 'Stop'
$receiverArguments = @(
    (Join-Path $PSScriptRoot 'tools\http_receiver.py'),
    '--bind', $Bind, '--port', "$Port", '--decode',
    '--timestamp-source', 'pc-clock', '--log-format', 'interval12',
    '--output', $Output
)
if ($DropFirstReceipt) { $receiverArguments += '--drop-first-receipt' }
& python @receiverArguments
exit $LASTEXITCODE
