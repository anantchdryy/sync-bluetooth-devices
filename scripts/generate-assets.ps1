Add-Type -AssemblyName System.Drawing

$project = Split-Path -Parent $PSScriptRoot
$windowsAssets = Join-Path $project 'desktop\gui\Assets'
$iosAssets = Join-Path $project 'ios\SyncAudioReceiver\Assets.xcassets\AppIcon.appiconset'
New-Item -ItemType Directory -Force -Path $windowsAssets, $iosAssets | Out-Null

function New-TandemBitmap([int]$size) {
    $bitmap = [System.Drawing.Bitmap]::new($size, $size)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.ScaleTransform($size / 1024.0, $size / 1024.0)
    $background = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        [System.Drawing.Rectangle]::new(0, 0, 1024, 1024),
        [System.Drawing.ColorTranslator]::FromHtml('#10243A'),
        [System.Drawing.ColorTranslator]::FromHtml('#174E5B'), 35.0)
    $graphics.FillRectangle($background, 0, 0, 1024, 1024)
    $mint = [System.Drawing.Pen]::new([System.Drawing.ColorTranslator]::FromHtml('#64E0C5'), 68)
    $white = [System.Drawing.Pen]::new([System.Drawing.ColorTranslator]::FromHtml('#F4FCFA'), 68)
    $mint.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $mint.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $white.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $white.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $path.AddBezier(170, 510, 280, 510, 285, 335, 394, 335)
    $path.AddBezier(394, 335, 505, 335, 506, 680, 618, 680)
    $graphics.DrawPath($mint, $path)
    $path.Reset()
    $path.AddBezier(406, 560, 510, 560, 518, 390, 626, 390)
    $path.AddBezier(626, 390, 738, 390, 744, 560, 854, 560)
    $graphics.DrawPath($white, $path)
    $path.Dispose()
    $mint.Dispose()
    $white.Dispose()
    $background.Dispose()
    $graphics.Dispose()
    return $bitmap
}

$large = New-TandemBitmap 1024
try {
    $large.Save((Join-Path $iosAssets 'AppIcon.png'),
                [System.Drawing.Imaging.ImageFormat]::Png)
} finally { $large.Dispose() }

$small = New-TandemBitmap 256
try {
    $memory = [System.IO.MemoryStream]::new()
    $small.Save($memory, [System.Drawing.Imaging.ImageFormat]::Png)
    $bytes = $memory.ToArray()
    $iconFile = [System.IO.File]::Create((Join-Path $windowsAssets 'TandemAudio.ico'))
    try {
        $writer = [System.IO.BinaryWriter]::new($iconFile)
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]1)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]$bytes.Length)
        $writer.Write([uint32]22)
        $writer.Write($bytes)
        $writer.Flush()
    } finally { $iconFile.Dispose(); $memory.Dispose() }
} finally { $small.Dispose() }
