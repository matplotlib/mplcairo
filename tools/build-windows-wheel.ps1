New-Item -ItemType directory build
Set-Location build
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Invoke-WebRequest `
    https://github.com/preshing/cairo-windows/releases/download/1.15.10/cairo-windows-1.15.10.zip `
    -OutFile cairo-windows-1.15.10.zip
Expand-Archive cairo-windows-1.15.10.zip -DestinationPath .
Invoke-WebRequest `
    https://github.com/ubawurinna/freetype-windows-binaries/releases/download/v2.9.1/freetype-2.9.1.zip `
    -OutFile freetype-2.9.1.zip
Expand-Archive freetype-2.9.1.zip -DestinationPath freetype-2.9.1
Set-Location ..
$Env:CL = `
    "/I$(Get-Location)\build\cairo-windows-1.15.10\include " `
  + "/I$(Get-Location)\build\freetype-2.9.1\include"
$Env:LINK = `
    "/LIBPATH:$(Get-Location)\build\cairo-windows-1.15.10\lib\x64 " `
  + "/LIBPATH:$(Get-Location)\build\freetype-2.9.1\win64"
python -mpip install --upgrade pybind11
python setup.py bdist_wheel
