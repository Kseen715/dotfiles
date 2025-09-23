. $PSScriptRoot/src/common.ps1

Invoke-ElevatedScript

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