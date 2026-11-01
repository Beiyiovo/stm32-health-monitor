# auto_watch_24h.ps1 — 501 设备自动守望器
# 每 60 秒探测一次 CH340 + $HR 数据帧；设备一插上就自动启动 24h 串口日志器。
# 停止方法：Stop-Process -Id (Get-Content 'T:\AS\bishe501\日志\watcher.pid')
$ErrorActionPreference = "SilentlyContinue"
$py = "python"
$probe = "T:\AS\毕业设计\AIGC\实验工作包\watcher_probe.py"
$bat   = "T:\AS\bishe501\serial_log.bat"
$flag  = "T:\AS\bishe501\日志\auto_started.flag"
$deadline = (Get-Date).AddHours(48)
$PID | Set-Content "T:\AS\bishe501\日志\watcher.pid"

while ((Get-Date) -lt $deadline) {
    if (Test-Path $flag) { break }   # 已由人工或其他实例启动则退出
    $out = & $py -X utf8 $probe 2>$null
    if ($out -match "DEVICE_OK (\S+)") {
        $port = $Matches[1]
        "AUTO_START $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') port=$port" | Set-Content $flag
        Start-Process -FilePath "cmd.exe" -ArgumentList "/c", "`"$bat`"" -WorkingDirectory "T:\AS\bishe501" -WindowStyle Minimized
        break
    }
    Start-Sleep -Seconds 60
}
