Add-Type -AssemblyName System.IO.Ports
$port = New-Object System.IO.Ports.SerialPort("COM20", 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$port.ReadTimeout = 200
$port.Open()
# Toggle DTR to trigger reset
$port.DtrEnable = $true
Start-Sleep -Milliseconds 200
$port.DtrEnable = $false
Write-Host ">>> board reset via DTR"
$endTime = (Get-Date).AddSeconds(12)
while ((Get-Date) -lt $endTime) {
    try {
        $line = $port.ReadLine()
        Write-Host $line.TrimEnd()
    } catch [System.TimeoutException] {
        # no data
    }
}
$port.Close()
Write-Host ">>> COM20 released"
