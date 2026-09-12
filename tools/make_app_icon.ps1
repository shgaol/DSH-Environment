# tools/make_app_icon.ps1
# Generates the DSH-Environment application icon: resources/app.ico
# (multi-size, PNG entries, Vista+). The artwork matches the runtime icon
# drawn by MainWindow::makeDIcon / CUINavBarItem::makeLetterIcon:
#   - rounded blue square #2563EB (rect 0.04S,0.04S,0.92S,0.92S, radius 0.22S)
#   - white bold letter D (pixel size ~0.75S, centered)
# Usage: pwsh -File tools/make_app_icon.ps1
# Also writes resources/app_icon_preview.png for visual check.
# NOTE: keep this file ASCII-only (English comments) so it parses
# correctly regardless of the PowerShell codepage.

Add-Type -AssemblyName System.Drawing

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir
$resDir     = Join-Path $projectDir 'resources'
$outIco     = Join-Path $resDir 'app.ico'
$outPreview = Join-Path $resDir 'app_icon_preview.png'

New-Item -ItemType Directory -Force -Path $resDir | Out-Null

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$bgColor = [System.Drawing.Color]::FromArgb(255, 0x25, 0x63, 0xEB)

# Draw one size of the D icon (32bpp ARGB)
function New-DIconBitmap([int]$size) {
    $bmp = [System.Drawing.Bitmap]::new($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    # Rounded-square path (matches Qt drawRoundedRect(0.04S,0.04S,0.92S,0.92S, 0.22S,0.22S))
    $x = $size * 0.04
    $y = $size * 0.04
    $w = $size * 0.92
    $h = $size * 0.92
    $r = $size * 0.22
    $d = $r * 2
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc($x, $y, $d, $d, 180, 90)
    $path.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $path.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $path.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $path.CloseFigure()

    $brush = New-Object System.Drawing.SolidBrush($bgColor)
    $g.FillPath($brush, $path)

    # White bold letter D (matches Qt: bold, pixelSize=int(0.75S), AlignCenter)
    $font = [System.Drawing.Font]::new('Segoe UI', [single]($size * 0.75), [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
    $sf = [System.Drawing.StringFormat]::new()
    $sf.Alignment = [System.Drawing.StringAlignment]::Center
    $sf.LineAlignment = [System.Drawing.StringAlignment]::Center
    $rect = [System.Drawing.RectangleF]::new(0, 0, $size, $size)
    $g.DrawString('D', $font, [System.Drawing.Brushes]::White, $rect, $sf)

    $font.Dispose()
    $sf.Dispose()
    $brush.Dispose()
    $path.Dispose()
    $g.Dispose()
    return $bmp
}

# Assemble the ICO (ICONDIR + ICONDIRENTRY list + PNG image data)
$entries = @()
$images  = @()
$offset  = 6 + 16 * $sizes.Count

foreach ($size in $sizes) {
    $bmp = New-DIconBitmap $size
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $png = $ms.ToArray()
    $ms.Dispose()
    $bmp.Dispose()

    $wh = if ($size -ge 256) { 0 } else { $size }   # 0 means 256
    $entry = New-Object byte[] 16
    $entry[0] = $wh
    $entry[1] = $wh
    $entry[2] = 0          # color count (0 = not applicable)
    $entry[3] = 0          # reserved
    [BitConverter]::GetBytes([uint16]1).CopyTo($entry, 4)   # planes
    [BitConverter]::GetBytes([uint16]32).CopyTo($entry, 6)  # bpp
    [BitConverter]::GetBytes([uint32]$png.Length).CopyTo($entry, 8)
    [BitConverter]::GetBytes([uint32]$offset).CopyTo($entry, 12)
    $entries += , $entry
    $images  += , $png
    $offset  += $png.Length
}

$fs = [System.IO.File]::Create($outIco)
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([uint16]0)                       # reserved
$bw.Write([uint16]1)                       # type = icon
$bw.Write([uint16]$sizes.Count)            # image count
foreach ($e in $entries) { $bw.Write($e) }
foreach ($im in $images) { $bw.Write($im) }
$bw.Close()
$fs.Close()

# Preview image (256x256 PNG)
$prev = New-DIconBitmap 256
$prev.Save($outPreview, [System.Drawing.Imaging.ImageFormat]::Png)
$prev.Dispose()

Write-Host "Generated: $outIco ($((Get-Item $outIco).Length) bytes)"
Write-Host "Preview:   $outPreview"
