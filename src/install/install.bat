@echo off
rem Installs the Japanese patch for Neverwinter Nights: Enhanced Edition.
rem
rem Ship this file together with nwnjp.dll and nwnjp_loader.exe in one folder, plus the
rem SEGA update patch (NWNJapanese1.69HotUUpdate.exe). Running install.bat then
rem   - copies nwnjp.dll and nwnjp_loader.exe next to nwmain.exe,
rem   - builds dialog.tlk / dialogf.tlk from the SEGA patch and installs them,
rem   - installs the OFL Japanese font into Documents\Neverwinter Nights\override.
rem
rem Usage: install.bat [-GameDir <path>] [-Patch <path>] [-OutDir <path>] [-Fonts]
rem
rem The PowerShell program is embedded below the "#PS1#" marker so that this file and the
rem two binaries are all that has to be shipped. Each run copies that part to a temporary
rem .ps1 and executes it; nothing else is written outside the cache and the game directory.
setlocal
set "SELF=%~f0"
set "NWNJP_HOME=%~dp0"
set "TMPPS=%TEMP%\nwnjp-install-%RANDOM%%RANDOM%.ps1"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$lines = Get-Content -LiteralPath $env:SELF -Encoding UTF8; $i = [Array]::IndexOf($lines, '#PS1#'); if ($i -lt 0) { exit 9 }; $body = $lines[($i + 1)..($lines.Count - 1)] -join [Environment]::NewLine; [System.IO.File]::WriteAllText($env:TMPPS, $body, (New-Object System.Text.UTF8Encoding($true)))"
if errorlevel 1 (
  echo Could not read the PowerShell part of this file.
  exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%TMPPS%" %*
set "R=%ERRORLEVEL%"
del /q "%TMPPS%" >nul 2>&1
if not "%R%"=="0" pause
exit /b %R%
#PS1#
# Builds a Japanese dialog.tlk and dialogf.tlk for Neverwinter Nights: Enhanced Edition
# from SEGA's Japanese 1.69 update patch and installs them next to the English TLK.
# The loader and the DLL that travel with this script are copied next to nwmain.exe.
#
# The patch is a 2005 NSIS installer that asks for administrator rights, so it is never run.
# Its two TLKs are read with 7-Zip: the small standalone 7zr.exe downloads the 7-Zip release,
# unpacks it into a cache directory, and that 7z.exe then reads the patch. Windows PowerShell
# 5.1 does the rest, so nothing has to be installed and nothing outside the cache and the
# game directory is touched.
#
# Usage:
#   install.bat                       install into the game found on this machine
#   install.bat -GameDir "D:\NWN"     use another game directory
#   install.bat -Patch "C:\x.exe"     use another copy of the SEGA patch
#   install.bat -OutDir "C:\tmp"      only build the two TLKs into a directory
#   install.bat -Fonts                reinstall the font even when it is already there
#
# Exit codes: 0 done, 1 usage or environment problem, 2 the patch or its contents differ.

param(
    [string]$GameDir,
    [string]$Patch,
    [string]$OutDir,
    [switch]$Fonts
)

$ErrorActionPreference = 'Stop'

# 7-Zip release used to read the patch. Pinned, because the URL and content must not move.
$SevenZipVersion = '26.06'
$SevenZipAsset = '7z' + $SevenZipVersion.Replace('.', '') + '-x64.exe'
$SevenZipUrl = 'https://github.com/ip7z/7zip/releases/download/' + $SevenZipVersion + '/' + $SevenZipAsset
$SevenZipSha256 = '9B697F65ECC2D9B0152F5EE96D72DDE522A7827690349D9307935DACF40F42EC'

# Standalone 7-Zip that reads 7z archives; used once to unpack the release above.
$SevenZrAsset = '7zr.exe'
$SevenZrUrl = 'https://github.com/ip7z/7zip/releases/download/' + $SevenZipVersion + '/' + $SevenZrAsset
$SevenZrSha256 = 'E80DB2DAD715A6C5ADABE292D31ED54E11F4E9B99CA349827B50573B175891BC'

$PatchName = 'NWNJapanese1.69HotUUpdate.exe'
$PatchUrl = 'https://web.archive.org/web/20090218131427/http://download.segaonline.jp/nwn/update/NWNJapanese1.69HotUUpdate.exe'
$PatchSha256 = '5D7CE7B432E0364CB91AA4AE2EB335E2AD655E067D0E4BCF9A4691BA18F61AC9'

# The 1.69 Japanese TLKs stored inside the patch; the build is only accepted when they match.
$SegaDialogSha256 = 'D7368C4B5E0CE28BFAE1EC947E86641E4FEC4104F0B34423A2BE5BF22ED91645'
$SegaDialogfSha256 = '50D390439B2E5DF6BC29CC7F676EA825E4E8963C5CEB6642426D86438D69737E'

# BIZ UD Gothic Regular from the Google Fonts repository, under the SIL Open Font License.
# Pinned to the commit that was checked, so the "main" branch moving cannot change the bytes.
$FontCommit = '965d8bbf9bc04dbac008adce48ba801ec09c8ecb'
$FontBase = 'https://raw.githubusercontent.com/googlefonts/morisawa-biz-ud-gothic/' + $FontCommit + '/'
$FontUrl = $FontBase + 'fonts/ttf/BIZUDGothic-Regular.ttf'
$FontSha256 = '7D2B48D84EF4E65C9F85BCF65FB1FB41C92B134BE641FAB6A7141D597C9A92B0'
$FontLicenseUrl = $FontBase + 'OFL.txt'
$FontLicenseSha256 = 'E753D7155D53C747D037A445E584C8ECFCA6DD79846DB610417E282A736B28BC'
$FontNames = @('fnt_default.ttf', 'fnt_default_hr.ttf', 'fnt_maintext.ttf')
$FontLicenseName = 'BIZUDGothic-OFL.txt'

# The two files that ship next to this script; they are copied next to nwmain.exe.
$BinaryNames = @('nwnjp.dll', 'nwnjp_loader.exe')

$HeaderSize = 20
$EntryStride = 40

$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$Utf8Strict = New-Object System.Text.UTF8Encoding($false, $true)
$Cp932 = [System.Text.Encoding]::GetEncoding(932)
$Cp1252 = [System.Text.Encoding]::GetEncoding(1252)

# The PowerShell part runs from a temporary file, so the folder holding this package comes
# from the batch wrapper rather than from the script path.
$ScriptDir = $env:NWNJP_HOME
if ($ScriptDir) {
    $ScriptDir = $ScriptDir.TrimEnd('\')
} else {
    $ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
}
$WorkDir = Join-Path ([System.IO.Path]::GetTempPath()) ('nwnjp-' + [guid]::NewGuid().ToString('N').Substring(0, 8))

function Write-Step([string]$Text) {
    Write-Host ''
    Write-Host ('== ' + $Text)
}

function Write-Note([string]$Text) {
    Write-Host ('   ' + $Text)
}

# SHA-256 through .NET rather than Get-FileHash: some Windows PowerShell 5.1 setups do not
# expose that cmdlet, and pinning the downloads matters more than the call being short.
function Get-Sha256([string]$Path) {
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
        $stream = [System.IO.File]::OpenRead($Path)
        try {
            $hash = $algorithm.ComputeHash($stream)
        } finally {
            $stream.Dispose()
        }
    } finally {
        $algorithm.Dispose()
    }
    return ([BitConverter]::ToString($hash) -replace '-', '')
}

function Get-RemoteFile([string]$Url, [string]$Destination) {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $client = New-Object System.Net.WebClient
    try {
        $client.Headers.Add('User-Agent', 'nwnjp-installer')
        $client.DownloadFile($Url, $Destination)
    } finally {
        $client.Dispose()
    }
}

function Read-Tlk([string]$Path) {
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 20 -or [System.Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne 'TLK ') {
        throw "not a TLK file: $Path"
    }
    [pscustomobject]@{
        Path       = $Path
        Bytes      = $bytes
        Count      = [int][BitConverter]::ToUInt32($bytes, 12)
        DataOffset = [int][BitConverter]::ToUInt32($bytes, 16)
    }
}

# Returns the raw text bytes of one entry, or $null when the entry has no text.
function Get-TlkString($Tlk, [int]$Index) {
    $entry = $HeaderSize + $Index * $EntryStride
    $flags = [BitConverter]::ToUInt32($Tlk.Bytes, $entry)
    if (($flags -band 1) -eq 0) { return $null }
    $offset = [int][BitConverter]::ToUInt32($Tlk.Bytes, $entry + 28)
    $size = [int][BitConverter]::ToUInt32($Tlk.Bytes, $entry + 32)
    if ($size -eq 0) { return $null }
    $start = $Tlk.DataOffset + $offset
    if ($start + $size -gt $Tlk.Bytes.Length) { return $null }
    $raw = New-Object byte[] $size
    [Array]::Copy($Tlk.Bytes, $start, $raw, 0, $size)
    return , $raw
}

# The template is the English EE table (cp1252) on a fresh install, but it is already UTF-8
# once this script has run and the backup has been consumed. Decide per string instead of
# guessing for the whole file.
function Convert-TemplateString([byte[]]$Raw) {
    if ($null -eq $Raw) { return $null }
    try {
        return $Utf8Strict.GetString($Raw)
    } catch {
        return $Cp1252.GetString($Raw)
    }
}

# Overlays the Japanese strings onto the template's table structure and writes a UTF-8 TLK.
function Build-Tlk([string]$TemplatePath, [string]$JapanesePath, [string]$OutputPath) {
    $template = Read-Tlk $TemplatePath
    $japanese = Read-Tlk $JapanesePath
    $count = $template.Count

    $blob = New-Object System.IO.MemoryStream
    $out = New-Object byte[] ($HeaderSize + $count * $EntryStride)
    [Array]::Copy($template.Bytes, 0, $out, 0, $HeaderSize)
    [BitConverter]::GetBytes([uint32]$count).CopyTo($out, 12)
    $dataOffset = $HeaderSize + $count * $EntryStride
    [BitConverter]::GetBytes([uint32]$dataOffset).CopyTo($out, 16)

    $japaneseCount = 0
    for ($i = 0; $i -lt $count; $i++) {
        $entry = $HeaderSize + $i * $EntryStride
        [Array]::Copy($template.Bytes, $entry, $out, $entry, $EntryStride)

        $text = $null
        if ($i -lt $japanese.Count) {
            $raw = Get-TlkString $japanese $i
            if ($null -ne $raw) {
                $decoded = $Cp932.GetString($raw)
                if ($decoded.Trim().Length -gt 0) {
                    $text = $Utf8NoBom.GetBytes($decoded)
                    $japaneseCount++
                }
            }
        }
        if ($null -eq $text) {
            $raw = Get-TlkString $template $i
            $decoded = Convert-TemplateString $raw
            if ($null -ne $decoded -and $decoded.Trim().Length -gt 0) {
                $text = $Utf8NoBom.GetBytes($decoded)
            }
        }

        if ($null -eq $text) {
            $flags = [BitConverter]::ToUInt32($out, $entry) -band 0xFFFFFFFE
            [BitConverter]::GetBytes([uint32]$flags).CopyTo($out, $entry)
            [BitConverter]::GetBytes([uint32]0).CopyTo($out, $entry + 28)
            [BitConverter]::GetBytes([uint32]0).CopyTo($out, $entry + 32)
            continue
        }
        [BitConverter]::GetBytes([uint32]$blob.Position).CopyTo($out, $entry + 28)
        [BitConverter]::GetBytes([uint32]$text.Length).CopyTo($out, $entry + 32)
        $blob.Write($text, 0, $text.Length)
    }

    $blobBytes = $blob.ToArray()
    [System.IO.File]::WriteAllBytes($OutputPath, ($out + $blobBytes))
    Write-Note (('{0}: {1} entries, {2} from the Japanese TLK, {3} bytes' -f `
                (Split-Path -Leaf $OutputPath), $count, $japaneseCount, ($out.Length + $blobBytes.Length)))
    return $count
}

# Finds a working 7z.exe: one already in the cache, or one unpacked from the pinned release.
# The release is a 7z self-extracting installer, so something that reads 7z has to come first.
# Windows' own tar.exe does not do on Windows 10 ("LZMA codec is unsupported"), so the small
# standalone 7zr.exe (7z only, no installer needed) fetches and unpacks it.
function Get-SevenZip([string]$CacheRoot) {
    $toolsDir = Join-Path $CacheRoot ('7zip-' + $SevenZipVersion)
    $sevenZip = Join-Path $toolsDir '7z.exe'
    $sevenZipDll = Join-Path $toolsDir '7z.dll'
    if ((Test-Path -LiteralPath $sevenZip) -and (Test-Path -LiteralPath $sevenZipDll)) {
        Write-Note ('cached 7-Zip: ' + $toolsDir)
        return $sevenZip
    }

    Write-Note ('downloading ' + $SevenZrAsset + ' (7z reader for the release)')
    $sevenZr = Join-Path $WorkDir $SevenZrAsset
    Get-RemoteFile $SevenZrUrl $sevenZr
    $sevenZrActual = Get-Sha256 $sevenZr
    if ($sevenZrActual -ne $SevenZrSha256) {
        throw ("7zr.exe does not match the expected SHA-256`n" +
               "  expected $SevenZrSha256`n  actual   $sevenZrActual")
    }

    Write-Note ('downloading 7-Zip ' + $SevenZipVersion + ' (' + $SevenZipAsset + ')')
    $archive = Join-Path $WorkDir $SevenZipAsset
    Get-RemoteFile $SevenZipUrl $archive

    $actual = Get-Sha256 $archive
    if ($actual -ne $SevenZipSha256) {
        throw ("the downloaded 7-Zip archive does not match the expected SHA-256`n" +
               "  expected $SevenZipSha256`n  actual   $actual")
    }

    New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null
    & $sevenZr x $archive ('-o' + $toolsDir) -y -bso0 -bsp0
    if ($LASTEXITCODE -ne 0) {
        throw '7zr.exe could not unpack the 7-Zip release'
    }
    if (-not (Test-Path -LiteralPath $sevenZip)) {
        throw ('7z.exe is missing after unpacking ' + $archive)
    }
    Write-Note ('using 7-Zip from ' + $toolsDir)
    return $sevenZip
}

# Reads the Steam library list so installs outside the default folder are found too.
function Get-SteamLibraries {
    $libraries = @()
    $steam = $null
    try {
        $steam = (Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -ErrorAction Stop).SteamPath
    } catch {
        $steam = $null
    }
    if ([string]::IsNullOrEmpty($steam)) { return $libraries }
    $steam = $steam -replace '/', '\'
    $libraries += $steam
    $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
    if (Test-Path -LiteralPath $vdf) {
        foreach ($line in Get-Content -LiteralPath $vdf) {
            if ($line -match '"path"\s+"([^"]+)"') {
                $libraries += ($matches[1] -replace '\\\\', '\')
            }
        }
    }
    return $libraries
}

function Find-GameDir {
    $candidates = @()
    if ($GameDir) { $candidates += $GameDir }
    foreach ($library in Get-SteamLibraries) {
        $candidates += (Join-Path $library 'steamapps\common\Neverwinter Nights')
    }
    $candidates += 'C:\Program Files (x86)\Steam\steamapps\common\Neverwinter Nights'
    $candidates += 'C:\Program Files\Steam\steamapps\common\Neverwinter Nights'
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate 'lang\en\data\dialog.tlk')) { return $candidate }
    }
    return $null
}

function Install-Tlk([string]$Source, [string]$Target) {
    $backup = $Target + '.nwnjp-backup'
    if ((Test-Path -LiteralPath $Target) -and -not (Test-Path -LiteralPath $backup)) {
        Copy-Item -LiteralPath $Target -Destination $backup -Force
        Write-Note ('backup: ' + $backup)
    }
    Copy-Item -LiteralPath $Source -Destination $Target -Force
    Write-Note ('installed: ' + $Target)
}

# The loader looks for the DLL next to itself, so both go beside nwmain.exe.
function Install-Binaries([string]$Game) {
    $targetDir = Join-Path $Game 'bin\win32'
    $missing = @()
    foreach ($name in $BinaryNames) {
        $source = Join-Path $ScriptDir $name
        if (-not (Test-Path -LiteralPath $source)) {
            $missing += $name
            continue
        }
        $target = Join-Path $targetDir $name
        Copy-Item -LiteralPath $source -Destination $target -Force
        Write-Note ('installed: ' + $target)
    }
    if ($missing.Count -gt 0) {
        Write-Host ''
        Write-Host ('Not found next to install.bat, so not copied: ' + ($missing -join ', '))
    }
}

# The legacy UI asks for one TTF in three sizes, all named after the resource it replaces.
# BIZ UD Gothic is under the SIL Open Font License, so it can ship and be fetched; the
# licence has to travel with the font, so it is written next to it.
function Install-Fonts([bool]$Force) {
    $overrideDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Neverwinter Nights\override'
    $missing = @()
    foreach ($name in $FontNames) {
        if (-not (Test-Path -LiteralPath (Join-Path $overrideDir $name))) { $missing += $name }
    }
    if ($missing.Count -eq 0 -and -not $Force) {
        Write-Note ('fonts already in place: ' + $overrideDir)
        return
    }

    New-Item -ItemType Directory -Force -Path $overrideDir | Out-Null
    $fontFile = Join-Path $WorkDir 'BIZUDGothic-Regular.ttf'
    Get-RemoteFile $FontUrl $fontFile
    $actual = Get-Sha256 $fontFile
    if ($actual -ne $FontSha256) {
        throw ('BIZ UD Gothic does not match the expected SHA-256' + "`n" + '  expected ' + $FontSha256 + "`n" + '  actual   ' + $actual)
    }
    foreach ($name in $FontNames) {
        Copy-Item -LiteralPath $fontFile -Destination (Join-Path $overrideDir $name) -Force
        Write-Note ('font: ' + (Join-Path $overrideDir $name))
    }

    $licenseFile = Join-Path $WorkDir $FontLicenseName
    Get-RemoteFile $FontLicenseUrl $licenseFile
    $licenseActual = Get-Sha256 $licenseFile
    if ($licenseActual -ne $FontLicenseSha256) {
        throw 'the font licence does not match the expected SHA-256'
    }
    Copy-Item -LiteralPath $licenseFile -Destination (Join-Path $overrideDir $FontLicenseName) -Force
    Write-Note ('font licence: ' + (Join-Path $overrideDir $FontLicenseName))
}

try {
    New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null

    Write-Step 'Inputs'
    $patchPath = $Patch
    if (-not $patchPath) {
        $local = Join-Path $ScriptDir $PatchName
        if (Test-Path -LiteralPath $local) { $patchPath = $local }
    }
    if (-not $patchPath -or -not (Test-Path -LiteralPath $patchPath)) {
        Write-Host ('The SEGA patch ({0}) was not found.' -f $PatchName)
        Write-Host 'Download it yourself and place it next to this script:'
        Write-Host ('  ' + $PatchUrl)
        Write-Host 'It is SEGA''s copyrighted translation, so this script does not download it.'
        exit 1
    }
    $patchFull = (Resolve-Path -LiteralPath $patchPath).Path
    $patchHash = Get-Sha256 $patchFull
    Write-Note ('patch: ' + $patchFull)
    if ($patchHash -ne $PatchSha256) {
        Write-Note ('note: SHA-256 is ' + $patchHash + ', which differs from the analysed copy')
        Write-Note '      the TLKs inside are checked instead, so this only has to be the same patch'
    }

    $sevenZip = Get-SevenZip (Join-Path $env:LOCALAPPDATA 'nwnjp')

    Write-Step 'Reading the TLKs out of the patch'
    $extractDir = Join-Path $WorkDir 'tlk'
    & $sevenZip x $patchFull ('-o' + $extractDir) '$0\dialog.tlk' '$0\dialogf.tlk' -y -bso0 -bsp0
    if ($LASTEXITCODE -ne 0) { throw '7-Zip could not extract the TLKs from the patch' }
    $segaDialog = Join-Path $extractDir '$0\dialog.tlk'
    $segaDialogf = Join-Path $extractDir '$0\dialogf.tlk'
    $dialogHash = Get-Sha256 $segaDialog
    $dialogfHash = Get-Sha256 $segaDialogf
    Write-Note ('dialog.tlk  ' + $dialogHash)
    Write-Note ('dialogf.tlk ' + $dialogfHash)
    if ($dialogHash -ne $SegaDialogSha256 -or $dialogfHash -ne $SegaDialogfSha256) {
        Write-Host ''
        Write-Host 'The TLKs in this file are not the 1.69 Japanese ones this build was checked against.'
        exit 2
    }

    Write-Step 'Locating the game'
    $game = Find-GameDir
    $dataDir = $null
    if ($game) {
        $dataDir = Join-Path $game 'lang\en\data'
        Write-Note ('game: ' + $game)
    } elseif (-not $OutDir) {
        Write-Host 'The game was not found. Pass -GameDir "<game folder>" or -OutDir "<folder>".'
        exit 1
    }

    # The pristine English table keeps the English text for the entries EE added after 1.69.
    $template = $null
    if ($dataDir) {
        $backup = Join-Path $dataDir 'dialog.tlk.nwnjp-backup'
        if (Test-Path -LiteralPath $backup) {
            $template = $backup
        } else {
            $template = Join-Path $dataDir 'dialog.tlk'
        }
    } else {
        Write-Host 'No game directory, so -OutDir is used for the results; a template is required.'
        exit 1
    }
    $templateInfo = Read-Tlk $template
    Write-Note ('template: ' + $template + ' (' + $templateInfo.Count + ' entries)')

    Write-Step 'Building the Japanese TLKs'
    $builtDialog = Join-Path $WorkDir 'dialog.tlk'
    $builtDialogf = Join-Path $WorkDir 'dialogf.tlk'
    Build-Tlk $template $segaDialog $builtDialog | Out-Null
    Build-Tlk $template $segaDialogf $builtDialogf | Out-Null

    if ($OutDir) {
        New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
        Copy-Item -LiteralPath $builtDialog -Destination (Join-Path $OutDir 'dialog.tlk') -Force
        Copy-Item -LiteralPath $builtDialogf -Destination (Join-Path $OutDir 'dialogf.tlk') -Force
        Write-Step 'Done'
        Write-Note ('wrote dialog.tlk and dialogf.tlk into ' + $OutDir)
    } else {
        Write-Step 'Installing the fonts'
        Install-Fonts ([bool]$Fonts)
        Write-Step 'Installing into the game'
        $dialogTarget = Join-Path $dataDir 'dialog.tlk'
        $dialogfTarget = Join-Path $dataDir 'dialogf.tlk'
        try {
            Install-Binaries $game
            Install-Tlk $builtDialog $dialogTarget
            Install-Tlk $builtDialogf $dialogfTarget
        } catch {
            Write-Host ''
            Write-Host 'Could not write into the game directory (administrator rights are required).'
            Write-Host 'Run install.bat from a terminal opened with "Run as administrator", or copy manually:'
            foreach ($name in $BinaryNames) {
                $source = Join-Path $ScriptDir $name
                if (Test-Path -LiteralPath $source) {
                    Write-Host ('  ' + $source + '  ->  ' + (Join-Path $game ('bin\win32\' + $name)))
                }
            }
            Write-Host ('  ' + $builtDialog + '  ->  ' + $dialogTarget)
            Write-Host ('  ' + $builtDialogf + '  ->  ' + $dialogfTarget)
            exit 1
        }
        Write-Host ''
        Write-Host ('Finished. Start the game through ' + (Join-Path $game 'bin\win32\nwnjp_loader.exe'))
    }
    exit 0
} finally {
    if (Test-Path -LiteralPath $WorkDir) {
        Remove-Item -LiteralPath $WorkDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

