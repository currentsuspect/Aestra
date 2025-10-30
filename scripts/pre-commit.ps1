param()

$ErrorActionPreference = 'Stop'

$blockedPatterns = @(
	'\.key$', '\.pem$', '\.pfx$', '\.jks$', '\.keystore$', '\.p12$',
	'\.crt$', '\.cer$', '\.asc$',
	'^\.env(\..*)?$',
	'(^|/)secrets(/|$)', '(^|/)credentials(/|$)',
	'(^|/)signing(/|$)', '(^|/)codesign(/|$)', '(^|/)notarize(/|$)', '(^|/)provisioning(/|$)',
	'(^|/)assets_premium(/|$)', '(^|/)assets_private(/|$)', '(^|/)weights(/|$)', '(^|/)models(/|$)',
	'\.(onnx|pt|pth|safetensors|tflite|pb)$'
) | ForEach-Object { [regex]::new($_, 'IgnoreCase') }

$staged = git diff --cached --name-only
if (-not $staged) { exit 0 }

$violations = @()
foreach ($file in $staged) {
	foreach ($re in $blockedPatterns) {
		if ($re.IsMatch($file)) {
			$violations += $file
			break
		}
	}
}

if ($violations.Count -gt 0) {
	Write-Host "Blocked sensitive files in commit:" -ForegroundColor Red
	$violations | Sort-Object -Unique | ForEach-Object { Write-Host " - $_" -ForegroundColor Yellow }
	Write-Host "Remove these files from the commit or move them to private repos." -ForegroundColor Red
	exit 1
}

$diff = git diff --cached
$secretHints = @(
	'AWS_(ACCESS|SECRET)_KEY',
	'AKIA[0-9A-Z]{16}',
	'SECRET[_-]?KEY',
	'PRIVATE[_-]?KEY',
	'-----BEGIN (RSA|OPENSSH|EC) PRIVATE KEY-----',
	'ghp_[0-9A-Za-z]{36,}',
	'github_pat_[0-9A-Za-z_]{20,}',
	'xox[baprs]-[0-9A-Za-z-]{10,}',
	'password\s*=\s*["'']?.{6,}["'']?'
) | ForEach-Object { [regex]::new($_, 'IgnoreCase') }

foreach ($re in $secretHints) {
	if ($re.IsMatch($diff)) {
		Write-Host "Potential secret detected in staged changes: $($re.ToString())" -ForegroundColor Red
		Write-Host "Remove or mask secrets before committing." -ForegroundColor Red
		exit 1
	}
}

exit 0
