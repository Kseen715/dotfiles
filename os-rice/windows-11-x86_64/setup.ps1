. $PSScriptRoot/src/common.ps1

Invoke-ElevatedScript

# Telemetry
InvokeMicroscript "disable-telemetry.ps1" $PSScriptRoot

# Diagnostics
InvokeMicroscript "disable-diag.ps1" $PSScriptRoot

# Disable dmwappushservice
InvokeMicroscript "disable-dmwappushservice.ps1" $PSScriptRoot

# Disable Windows Search
InvokeMicroscript "disable-windows-search.ps1" $PSScriptRoot

# Disable Superfetch (SysMain)
InvokeMicroscript "disable-superfetch.ps1" $PSScriptRoot

# Disable Fax Service
# InvokeMicroscript "disable-fax.ps1" $PSScriptRoot

# Disable Auto Update of Windows
InvokeMicroscript "disable-auto-updates.ps1" $PSScriptRoot

# Explorer
InvokeRegMicroscript "reg-hide-file-extension.ps1" 0 $PSScriptRoot
InvokeRegMicroscript "reg-show-hidden.ps1" 1 $PSScriptRoot

# Taskbar
InvokeRegMicroscript "reg-cortana-button.ps1" 0 $PSScriptRoot
InvokeRegMicroscript "reg-taskbar-end-task.ps1" 1 $PSScriptRoot
InvokeRegMicroscript "reg-window-disallow-shaking.ps1" 1 $PSScriptRoot

# Multitasking / Snap settings
InvokeRegMicroscript "reg-enable-task-groups.ps1" 0 $PSScriptRoot
InvokeRegMicroscript "reg-snap-assist.ps1" 0 $PSScriptRoot
InvokeRegMicroscript "reg-enable-snap-bar.ps1" 0 $PSScriptRoot
InvokeRegMicroscript "reg-enable-snap-assist-flyout.ps1" 0 $PSScriptRoot
InvokeRegMicroscript "reg-soft-bound-snap.ps1" 0 $PSScriptRoot

# Sudo
InvokeRegMicroscript "reg-sudo.ps1" 3 $PSScriptRoot

# # Debloat Windows 11
# InvokeMicroscript "raphire-win11debloat.ps1" $PSScriptRoot