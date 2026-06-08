param (
    [string]$sourcePngPath,
    [string]$targetIcoPath
)

Add-Type -AssemblyName System.Drawing

# Sizes to generate
$sizes = @(16, 32, 48, 64, 128, 256)

# Load source image
$srcImg = [System.Drawing.Image]::FromFile($sourcePngPath)

# Array to hold PNG byte arrays and metadata
$imagesData = @()

foreach ($size in $sizes) {
    # Create target bitmap
    $bmp = New-Object System.Drawing.Bitmap($size, $size)
    $bmp.SetResolution(96, 96)
    
    # Create graphics and render original image with high quality settings
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    
    # Draw scaled image
    $g.DrawImage($srcImg, 0, 0, $size, $size)
    $g.Dispose()
    
    # Save bitmap to MemoryStream in PNG format
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    
    $pngBytes = $ms.ToArray()
    $ms.Dispose()
    
    # Width and Height as single bytes (256 is represented by 0)
    $wByte = if ($size -eq 256) { 0 } else { [byte]$size }
    $hByte = if ($size -eq 256) { 0 } else { [byte]$size }
    
    $imagesData += [PSCustomObject]@{
        Width      = $wByte
        Height     = $hByte
        Bytes      = $pngBytes
        Length     = $pngBytes.Length
    }
}

$srcImg.Dispose()

# Create target file stream
$fs = New-Object System.IO.FileStream($targetIcoPath, [System.IO.FileMode]::Create)
$bw = New-Object System.IO.BinaryWriter($fs)

# Write ICO Header
$bw.Write([uint16]0)      # Reserved (always 0)
$bw.Write([uint16]1)      # Type (1 = Icon)
$bw.Write([uint16]$sizes.Count) # Number of images

# Calculate starting offset for image data
# Header is 6 bytes. Each directory entry is 16 bytes.
$currentOffset = 6 + ($sizes.Count * 16)

# Write Directory Entries
foreach ($img in $imagesData) {
    $bw.Write([byte]$img.Width)      # Width
    $bw.Write([byte]$img.Height)     # Height
    $bw.Write([byte]0)               # Color count (0 for PNG/256+ colors)
    $bw.Write([byte]0)               # Reserved (always 0)
    $bw.Write([uint16]1)             # Color planes (1)
    $bw.Write([uint16]32)            # Bits per pixel (32)
    $bw.Write([uint32]$img.Length)   # Size of image data in bytes
    $bw.Write([uint32]$currentOffset) # Offset of image data from start of file
    
    $currentOffset += $img.Length
}

# Write Image Data (PNG blocks)
foreach ($img in $imagesData) {
    $bw.Write($img.Bytes, 0, $img.Length)
}

$bw.Close()
$fs.Close()

Write-Host "ICO generated successfully at: $targetIcoPath"
