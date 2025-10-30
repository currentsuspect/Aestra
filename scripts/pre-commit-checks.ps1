<#
Pre-commit checks: run from repo root. Scans staged files for common sensitive patterns
Exit code: 0 = ok, non-zero = block commit
#>
param()

Write-Host "Running pre-commit checks..."

# Get staged files
$staged = git diff --cached --name-only
if (-not $staged) {
    Write-Host "No staged files." -ForegroundColor Yellow
    exit 0
}

$patterns = @(
    '-----BEGIN .*PRIVATE KEY-----',
    '-----BEGIN RSA PRIVATE KEY-----',
    'BEGIN PRIVATE KEY',
    '\.pfx$',
    '\.p12$',
    '\.pem$',
    '\.key$',
    'NomadCert',
    'AKIA[A-Z0-9]{16}', # AWS Access Key ID heuristic
    'ssh-rsa AAAA',
    'api_key',
    'API_KEY',
    'password\s*=\s*',
    '\.onnx$',
    '\.pt$',
    '\.pth$',
    '\.h5$',
    '\.ckpt$'
)

$failures = @()

foreach ($f in $staged) {
    if (Test-Path $f) {
        $text = Get-Content -Path $f -Raw -ErrorAction SilentlyContinue
        if ($null -eq $text) { continue }

        foreach ($p in $patterns) {
            if ($text -match $p) {
                $failures += "$f => pattern: $p"
            }
        }

        # filename checks for binary extensions
        foreach ($ext in @('.pfx','.p12','.pem','.key','.onnx','.pt','.pth','.h5','.ckpt')) {
            if ($f.ToLower().EndsWith($ext)) { $failures += "$f => blocked extension $ext" }
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Pre-commit check failed. Sensitive patterns found:" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host $_ }
    Write-Host "If this is a false positive, review and stage amended files. Otherwise remove secrets and try again." -ForegroundColor Yellow
    exit 1
}

Write-Host "Pre-commit checks passed." -ForegroundColor Green
exit 0
