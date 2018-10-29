New-Item -ItemType directory -Force build | Out-Null
Set-Location build
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$cairo_url = `
    "https://github.com/preshing/cairo-windows/releases/download/1.15.12"
$cairo_name = "cairo-windows-1.15.12"
if (!(Test-Path "$cairo_name")) {
    Invoke-WebRequest "$cairo_url/$cairo_name.zip" -OutFile "$cairo_name.zip"
    Expand-Archive "$cairo_name.zip" -DestinationPath .
}

$freetype_url = `
    "https://github.com/ubawurinna/freetype-windows-binaries/releases/download/v2.9.1"
$freetype_name = "freetype-2.9.1"
if (!(Test-Path "$freetype_name")) {
    Invoke-WebRequest "$freetype_url/$freetype_name.zip" -OutFile "$freetype_name.zip"
    Expand-Archive "$freetype_name.zip" -DestinationPath "$freetype_name"
}

Set-Location ..

$Env:CL = `
    "/I$(Get-Location)\build\$cairo_name\include " `
  + "/I$(Get-Location)\build\$freetype_name\include"
$Env:LINK = `
    "/LIBPATH:$(Get-Location)\build\$cairo_name\lib\x64 " `
  + "/LIBPATH:$(Get-Location)\build\$freetype_name\win64"
python -mpip install --upgrade pybind11
python setup.py bdist_wheel
