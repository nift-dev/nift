$ErrorActionPreference = 'Stop'

$version = '__VERSION__'
$packageArgs = @{
  packageName    = $env:ChocolateyPackageName
  url64bit       = "https://github.com/nift-dev/nift/releases/download/v$version/nift-$version-windows-x86_64.zip"
  checksum64     = '__CHECKSUM64__'
  checksumType64 = 'sha256'
  unzipLocation  = "$(Split-Path -Parent $MyInvocation.MyCommand.Definition)"
}

Install-ChocolateyZipPackage @packageArgs
