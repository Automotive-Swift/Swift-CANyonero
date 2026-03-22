Write-Host "=== Checking Certificate Stores ===" -ForegroundColor Cyan

Write-Host "`nTrusted Root CA:"
Get-ChildItem 'Cert:\LocalMachine\Root' | Where-Object { $_.Subject -like '*ECUconnect*' } | Format-List Subject, Thumbprint, NotAfter

Write-Host "`nTrusted Publishers:"
Get-ChildItem 'Cert:\LocalMachine\TrustedPublisher' | Where-Object { $_.Subject -like '*ECUconnect*' } | Format-List Subject, Thumbprint, NotAfter

Write-Host "`nUser Certificate Store:"
Get-ChildItem 'Cert:\CurrentUser\My' | Where-Object { $_.Subject -like '*ECUconnect*' } | Format-List Subject, Thumbprint, NotAfter

Write-Host "`n=== Driver Signature Check ===" -ForegroundColor Cyan
$signtool = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe"
$driver = "C:\Users\DrMic\Documents\late\Automotive-Swift\Swift-CANyonero\Sources\ecuconnect-j2534\driver\x64\Release\ecuconnect_l2cap.sys"

if (Test-Path $signtool) {
    & $signtool verify /pa /v $driver 2>&1 | Select-String -Pattern 'Signature|Issued|Hash|Signed|Error|Success'
}
