<#
    WideStrike - static patch-site verifier

    Reads jsf.exe from disk (no execution) and confirms that every byte
    WideStrike patches is where it expects it to be.

    Usage:  powershell -ExecutionPolicy Bypass -File tools\verify.ps1 [-Exe path]
#>
param([string]$Exe = "..\jsf.exe")

$ErrorActionPreference = "Stop"
if (-not (Test-Path $Exe)) { $Exe = Join-Path (Split-Path $PSScriptRoot -Parent) "..\jsf.exe" }
if (-not (Test-Path $Exe)) { Write-Error "jsf.exe not found: $Exe"; exit 2 }

$bytes = [IO.File]::ReadAllBytes((Resolve-Path $Exe))
"exe      : $((Resolve-Path $Exe).Path)"
"size     : $($bytes.Length)  (expected 1072128)"
if ($bytes.Length -ne 1072128) { "!! size mismatch - patches are for a different build"; }

$lfanew = [BitConverter]::ToInt32($bytes, 0x3C)
$nsec   = [BitConverter]::ToUInt16($bytes, $lfanew + 6)
$optSize= [BitConverter]::ToUInt16($bytes, $lfanew + 20)
$magic  = [BitConverter]::ToUInt16($bytes, $lfanew + 24)
$sectTab= $lfanew + 24 + $optSize
$secs = @()
for ($i = 0; $i -lt $nsec; $i++) {
    $o = $sectTab + $i * 40
    $name = [Text.Encoding]::ASCII.GetString($bytes, $o, 8).TrimEnd([char]0)
    $secs += [pscustomobject]@{
        Name = $name
        VA   = [BitConverter]::ToUInt32($bytes, $o + 12)
        VSz  = [BitConverter]::ToUInt32($bytes, $o + 8)
        Raw  = [BitConverter]::ToUInt32($bytes, $o + 20)
        RSz  = [BitConverter]::ToUInt32($bytes, $o + 16)
    }
}

function RvaToOff([uint32]$rva) {
    foreach ($s in $secs) {
        $span = [Math]::Max($s.VSz, $s.RSz)
        if ($rva -ge $s.VA -and $rva -lt ($s.VA + $span)) { return [int]($rva - $s.VA + $s.Raw) }
    }
    return -1
}
function Peek([uint32]$rva, [int]$n) {
    $o = RvaToOff $rva
    if ($o -lt 0) { return $null }
    return $bytes[$o..($o + $n - 1)]
}
function Hex([byte[]]$b) { if ($null -eq $b) { return "<oob>" } ; ($b | ForEach-Object { $_.ToString("X2") }) -join " " }

$fail = 0
function Check($label, [uint32]$rva, [byte[]]$expect) {
    $got = Peek $rva $expect.Length
    $ok  = $null -ne $got
    if ($ok) { for ($i = 0; $i -lt $expect.Length; $i++) { if ($got[$i] -ne $expect[$i]) { $ok = $false; break } } }
    if ($ok) { "  [ok]   $label  @0x$("{0:X8}" -f (0x400000 + $rva))" }
    else {
        "  [FAIL] $label  @0x$("{0:X8}" -f (0x400000 + $rva))  want $(Hex $expect)  got $(Hex $got)"
        $script:fail++
    }
}

"`npatch sites:"
# call sub_41B8A0  (fov hook)
Check "projection call site"        0x1BFE2 ([byte[]](0xE8,0x49,0xEF,0xFF,0xFF))
# mov eax, offset sub_4B0B50 (mode callback)
Check "mode callback immediate"     0xB16DD ([byte[]](0x50,0x0B,0x4B,0x00))
# force software: jnz at 0x419C08
Check "render-mode branch (jnz)"    0x19C08 ([byte[]](0x0F,0x85))

"`nclip guards (cmp r32,4000h ; jg):"
foreach ($imm in 0x4889C,0x488B2,0x5032D,0x50345,0x50E7F,0x50E99) {
    $b = Peek ($imm - 2) 8
    $ok = ($null -ne $b) -and $b[0] -eq 0x81 -and ((($b[1] -shr 3) -band 7) -eq 7) -and
          $b[2] -eq 0x00 -and $b[3] -eq 0x40 -and $b[4] -eq 0x00 -and $b[5] -eq 0x00 -and
          $b[6] -eq 0x0F -and $b[7] -eq 0x8F
    if ($ok) { "  [ok]   guard @0x$("{0:X8}" -f (0x400000 + $imm))" }
    else { "  [FAIL] guard @0x$("{0:X8}" -f (0x400000 + $imm))  got $(Hex $b)"; $fail++ }
}

# ------------------------------------------------------------------
# projection sanity: reproduce sub_41B8A0 and the WideStrike fix
# ------------------------------------------------------------------
$fovUnits = 1024.0                      # double at camera+0xA4 (0x4090000000000000)
$pi       = 3.14159265358979323846
$htanStock= $fovUnits * $pi / 4096.0    # == 0.785398...
$vtanStock= 0.75 * $htanStock

"`nprojection (stock 4:3 ratio = 0.75):"
"  htan {0:F5}  half-angle {1:F2} deg, full hfov {2:F2} deg" -f $htanStock, ([math]::Atan($htanStock)*180/$pi), ([math]::Atan($htanStock)*360/$pi)
foreach ($m in @(@(640,480),@(800,600),@(1280,720),@(1920,1080),@(2560,1440),@(3440,1440))) {
    $w = $m[0]; $h = $m[1]
    $xsStock = ($w / 2.0) / $htanStock
    $ysStock = ($h / 2.0) / $vtanStock
    $newHtan = $vtanStock * $w / $h
    $xsFixed = ($w / 2.0) / $newHtan
    $stretch = $xsStock / $ysStock
    "  {0,4}x{1,-4}  stock x/y {2,6:F1}/{3,6:F1} (stretch {4,4:F2}x)  ->  fixed {5,6:F1}/{6,6:F1} (stretch {7,4:F2}x)  hfov {8,5:F1} deg" -f `
        $w,$h,$xsStock,$ysStock,$stretch,$xsFixed,$ysStock,($xsFixed/$ysStock),([math]::Atan($newHtan)*360/$pi)
}

""
if ($fail -eq 0) { "RESULT: all patch sites verified" ; exit 0 }
else { "RESULT: $fail problem(s) - WideStrike will skip the mismatching patch" ; exit 1 }
