param(
  [Parameter(Mandatory = $true)][string]$Source,
  [Parameter(Mandatory = $true)][string]$Destination,
  [int]$Maximum = 256
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$image = $null
$bitmap = $null
$graphics = $null
$attributes = $null
try {
  $image = [System.Drawing.Image]::FromFile([IO.Path]::GetFullPath($Source))
  $ratio = [Math]::Min(1.0, $Maximum / [double][Math]::Max($image.Width, $image.Height))
  $width = [Math]::Max(1, [int][Math]::Round($image.Width * $ratio))
  $height = [Math]::Max(1, [int][Math]::Round($image.Height * $ratio))
  $bitmap = New-Object System.Drawing.Bitmap($width, $height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
  $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
  $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
  $attributes = New-Object System.Drawing.Imaging.ImageAttributes
  $attributes.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)
  $rectangle = New-Object System.Drawing.Rectangle(0, 0, $width, $height)
  $graphics.DrawImage($image, $rectangle, 0, 0, $image.Width, $image.Height, [System.Drawing.GraphicsUnit]::Pixel, $attributes)
  $bitmap.Save([IO.Path]::GetFullPath($Destination), [System.Drawing.Imaging.ImageFormat]::Png)
} finally {
  if ($null -ne $attributes) { $attributes.Dispose() }
  if ($null -ne $graphics) { $graphics.Dispose() }
  if ($null -ne $bitmap) { $bitmap.Dispose() }
  if ($null -ne $image) { $image.Dispose() }
}
