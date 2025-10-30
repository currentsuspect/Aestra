$copyrightHeader = @"
// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

"@

# Get all .cpp and .h files
$files = Get-ChildItem -Path . -Include *.cpp,*.h -Recurse -File

foreach ($file in $files) {
    $content = Get-Content -Path $file.FullName -Raw
    
    # Skip if already has copyright
    if ($content -match "© 2025 Nomad Studios") {
        Write-Host "Skipping (already has copyright): $($file.FullName)"
        continue
    }
    
    # Skip if file starts with a shebang
    if ($content -match '^#!') {
        $lines = $content -split "`n"
        $newContent = $lines[0] + "`n" + $copyrightHeader + ($lines[1..$lines.Length] -join "`n")
    } else {
        $newContent = $copyrightHeader + $content
    }
    
    # Write the content back to the file
    [System.IO.File]::WriteAllText($file.FullName, $newContent)
    Write-Host "Added copyright to: $($file.FullName)"
}

Write-Host "Copyright headers added to all source files."
