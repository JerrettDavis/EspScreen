Add-Type -AssemblyName System.IO.Ports
$port = New-Object System.IO.Ports.SerialPort("COM20", 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$port.ReadTimeout = 200
$port.Open()
Start-Sleep -Milliseconds 800
$port.WriteLine("cal")
Write-Host ">>> sent: cal"
$endTime = (Get-Date).AddSeconds(15)
while ((Get-Date) -lt $endTime) {
    try {
        $line = $port.ReadLine()
        Write-Host $line.TrimEnd()
    } catch [System.TimeoutException] {
        # no data yet, keep polling
    }
}
$port.Close()
Write-Host ">>> COM20 released"
