# otoca-camhook local build (MSVC, 32-bit: the game is x86)
$ErrorActionPreference = "Stop"
$vs = "C:\Program Files\Microsoft Visual Studio\18\Enterprise"

Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation `
    -DevCmdArguments "-arch=x86 -host_arch=x64" | Out-Null

Set-Location $PSScriptRoot

# Static CRT (/MT): no VC runtime DLL needed next to the game.
cmake -B build -S . -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
if ($LASTEXITCODE -ne 0) { throw "configure failed ($LASTEXITCODE)" }

cmake --build build
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }

Get-Item build\otoca-camhook.dll, build\otoca-scan.exe, build\qrtest.exe | ForEach-Object {
    Write-Output "BUILT: $($_.FullName) ($([math]::Round($_.Length/1kb)) KB)"
}
