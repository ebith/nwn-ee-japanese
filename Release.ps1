# Packs the release archive from a fresh dist\ (run src\build.bat first).
# This runs on the development machine, which has 7z.exe on PATH.
$ErrorActionPreference = 'Stop'

$Name = 'NWN-EE-Japanese'
$Archive = "$Name.7z"
$Tag = 'v' + (Get-Date -Format 'yyyyMMdd-HHmm')

# Only these three are shipped; dist\ also holds intermediate files.
$Files = @('install.bat', 'nwnjp.dll', 'nwnjp_loader.exe')

if (git status --porcelain | Where-Object { $_ -notmatch '^\?\?' }) {
    throw 'the working tree has uncommitted changes in tracked files'
}
foreach ($file in $Files) {
    if (-not (Test-Path -LiteralPath (Join-Path 'dist' $file))) {
        throw ('dist\{0} is missing; run src\build.bat first' -f $file)
    }
}

if (Test-Path -LiteralPath $Archive) { Remove-Item -LiteralPath $Archive -Force }
& 7z.exe a -t7z -mx7 $Archive ($Files | ForEach-Object { Join-Path 'dist' $_ })
& 7z.exe rn $Archive dist $Name

gh release create $Tag $Archive --generate-notes
