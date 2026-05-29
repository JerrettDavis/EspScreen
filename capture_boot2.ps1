Add-Type -AssemblyName System.IO.Ports
$port = New-Object System.IO.Ports.SerialPort("COM20", 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$port.ReadTimeout = 100
$port.Open()
Start-Sleep -Milliseconds 100

# Reset via RTS (esptool-style: RTS=high pulls EN low on most ESP32 devkits)
$port.RtsEnable = $true
Start-Sleep -Milliseconds 200
$port.RtsEnable = $false
Write-Host ">>> board reset via RTS"

$endTime = (Get-Date).AddSeconds(12)
while ((Get-Date) -lt $endTime) {
    try {
        $line = $port.ReadLine()
        Write-Host $line.TrimEnd()
    } catch [System.TimeoutException] {
        # no data, keep polling
    }
}
$port.Close()
Write-Host ">>> COM20 released"
