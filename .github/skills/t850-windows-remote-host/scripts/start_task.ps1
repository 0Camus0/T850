# Starts a script in the logged-on user's console session through a scheduled task.
# windows_parity.py `run` calls it over SSH, which runs in session 0, where DXGI cannot create a swapchain.
# Install once as <root>\start_task.ps1 on the Windows host; the SSH user must be logged on to the console.
param([string]$Name, [string]$Script)
$action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -ExecutionPolicy Bypass -File `"$Script`""
$principal = New-ScheduledTaskPrincipal -UserId "$env:COMPUTERNAME\$env:USERNAME" -LogonType Interactive -RunLevel Highest
Register-ScheduledTask -TaskName $Name -Action $action -Principal $principal -Force | Out-Null
Start-ScheduledTask -TaskName $Name
"started $Name as $env:COMPUTERNAME\$env:USERNAME"
