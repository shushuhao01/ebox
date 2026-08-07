Add-Type -AssemblyName System.Drawing

$outDir = "d:\Projects\2Box-master\_icon_tmp"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$canvas = 512

# Draw a monitor (bezel + screen + stand)
function Draw-Monitor {
    param($g, $x, $y, $w, $h, $bezelBrush, $screenBrush, $standBrush, $accentBrush, $contentBrush, $penBezel)

    $bezelX = $x; $bezelY = $y
    $bezelW = $w; $bezelH = $h

    # stand (draw first, behind body)
    $cx = $bezelX + $bezelW / 2
    $g.FillRectangle($standBrush, $cx - $bezelW * 0.055, $bezelY + $bezelH * 0.92, $bezelW * 0.11, $bezelH * 0.14)
    $g.FillRectangle($standBrush, $cx - $bezelW * 0.24, $bezelY + $bezelH * 1.06, $bezelW * 0.48, $bezelH * 0.06)

    # bezel (rounded)
    $bPath = New-Object System.Drawing.Drawing2D.GraphicsPath
    $r = $bezelW * 0.06
    $d = $r * 2
    $bPath.AddArc($bezelX, $bezelY, $d, $d, 180, 90)
    $bPath.AddArc($bezelX + $bezelW - $d, $bezelY, $d, $d, 270, 90)
    $bPath.AddArc($bezelX + $bezelW - $d, $bezelY + $bezelH - $d, $d, $d, 0, 90)
    $bPath.AddArc($bezelX, $bezelY + $bezelH - $d, $d, $d, 90, 90)
    $bPath.CloseFigure()
    $g.FillPath($bezelBrush, $bPath)
    $bPath.Dispose()

    # screen
    $sx = $bezelX + $bezelW * 0.08
    $sy = $bezelY + $bezelH * 0.10
    $sw = $bezelW * 0.84
    $sh = $bezelH * 0.78
    $sPath = New-Object System.Drawing.Drawing2D.GraphicsPath
    $r2 = $sw * 0.04
    $d2 = $r2 * 2
    $sPath.AddArc($sx, $sy, $d2, $d2, 180, 90)
    $sPath.AddArc($sx + $sw - $d2, $sy, $d2, $d2, 270, 90)
    $sPath.AddArc($sx + $sw - $d2, $sy + $sh - $d2, $d2, $d2, 0, 90)
    $sPath.AddArc($sx, $sy + $sh - $d2, $d2, $d2, 90, 90)
    $sPath.CloseFigure()
    $g.FillPath($screenBrush, $sPath)
    $sPath.Dispose()

    # screen content - small app windows
    $pad = $sw * 0.10
    $winW = ($sw - $pad * 3) / 2
    $winH = ($sh - $pad * 3) / 2
    # top-left window
    $g.FillRectangle($accentBrush, $sx + $pad, $sy + $pad, $winW, $winH)
    # bottom-right window
    $g.FillRectangle($accentBrush, $sx + $pad * 2 + $winW, $sy + $pad * 2 + $winH, $winW, $winH)
    # lines inside top-left window
    $lineX = $sx + $pad + $winW * 0.12
    $lineY = $sy + $pad + $winH * 0.55
    $pen = [System.Drawing.Pen]::new($contentBrush, [float]($winH * 0.10))
    $g.DrawLine($pen, $lineX, $lineY, $lineX + $winW * 0.76, $lineY)
    $g.DrawLine($pen, $lineX, $lineY + $winH * 0.22, $lineX + $winW * 0.55, $lineY + $winH * 0.22)
    $pen.Dispose()
}

# Render base 512 canvas
$bmp = New-Object System.Drawing.Bitmap($canvas, $canvas, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias

# background rounded square (solid, opaque)
$bgBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 24, 55, 96))
$bgPath = New-Object System.Drawing.Drawing2D.GraphicsPath
$m = 26
$bgR = 100
$bgD = $bgR * 2
$bgPath.AddArc($m, $m, $bgD, $bgD, 180, 90)
$bgPath.AddArc($canvas - $m - $bgD, $m, $bgD, $bgD, 270, 90)
$bgPath.AddArc($canvas - $m - $bgD, $canvas - $m - $bgD, $bgD, $bgD, 0, 90)
$bgPath.AddArc($m, $canvas - $m - $bgD, $bgD, $bgD, 90, 90)
$bgPath.CloseFigure()
$g.FillPath($bgBrush, $bgPath)
$bgPath.Dispose()
$bgBrush.Dispose()

# back monitor (upper-right, gray)
$backBezel = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 226, 232, 240))
$backScreen = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 13, 40, 74))
$backStand = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 148, 163, 184))
$backAccent = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 62, 142, 224))
$backContent = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 160, 210, 250))

Draw-Monitor -g $g -x 150 -y 62 -w 268 -h 196 -bezelBrush $backBezel -screenBrush $backScreen -standBrush $backStand -accentBrush $backAccent -contentBrush $backContent

# front monitor (lower-left, blue, overlaps back)
$frontBezel = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 33, 150, 243))
$frontScreen = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 232, 244, 255))
$frontStand = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 21, 118, 210))
$frontAccent = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 21, 101, 192))
$frontContent = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 66, 165, 245))

Draw-Monitor -g $g -x 62 -y 150 -w 276 -h 200 -bezelBrush $frontBezel -screenBrush $frontScreen -standBrush $frontStand -accentBrush $frontAccent -contentBrush $frontContent

$backBezel.Dispose(); $backScreen.Dispose(); $backStand.Dispose(); $backAccent.Dispose(); $backContent.Dispose()
$frontBezel.Dispose(); $frontScreen.Dispose(); $frontStand.Dispose(); $frontAccent.Dispose(); $frontContent.Dispose()
$g.Dispose()

# save per-size PNGs
$sizes = @(16, 24, 32, 48, 64, 128, 256)
$pngPaths = @()
foreach ($sz in $sizes) {
    $small = New-Object System.Drawing.Bitmap($sz, $sz, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $sg = [System.Drawing.Graphics]::FromImage($small)
    $sg.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $sg.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $sg.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $sg.DrawImage($bmp, 0, 0, $sz, $sz)
    $sg.Dispose()
    $path = Join-Path $outDir ("icon_{0}.png" -f $sz)
    $small.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $small.Dispose()
    $pngPaths += $path
}
$bmp.Dispose()

# assemble ICO (PNG-compressed entries)
$count = $pngPaths.Count
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter($ms)
$bw.Write([uint16]0)
$bw.Write([uint16]1)
$bw.Write([uint16]$count)
$offset = 6 + 16 * $count
$allBytes = @()
foreach ($p in $pngPaths) {
    $bytes = [System.IO.File]::ReadAllBytes($p)
    $allBytes += , $bytes
}
for ($i = 0; $i -lt $count; $i++) {
    $img = [System.Drawing.Image]::FromFile($pngPaths[$i])
    $w = $img.Width
    $h = $img.Height
    $size = $allBytes[$i].Length
    if ($w -ge 256) { $bw.Write([byte]0) } else { $bw.Write([byte]$w) }
    if ($h -ge 256) { $bw.Write([byte]0) } else { $bw.Write([byte]$h) }
    $bw.Write([byte]0)
    $bw.Write([byte]0)
    $bw.Write([uint16]1)
    $bw.Write([uint16]32)
    $bw.Write([uint32]$size)
    $bw.Write([uint32]$offset)
    $offset += $size
    $img.Dispose()
}
foreach ($b in $allBytes) { $bw.Write($b) }
$bw.Flush()
[System.IO.File]::WriteAllBytes("d:\Projects\2Box-master\eBox\res\eBox.ico", $ms.ToArray())
$bw.Dispose()
$ms.Dispose()

Write-Host "ICO generated: d:\Projects\2Box-master\eBox\res\eBox.ico"
