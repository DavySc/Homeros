# What the recordings share (trace.ps1, pg.ps1): Visual Studio through its automation object (EnvDTE), with its settings as installed.
# Visual Studio answers "busy" to a call that comes while it works: ask again
function Again($what) { for ($i = 0; $i -lt 200; $i++) { try { return & $what } catch { Start-Sleep -Milliseconds 100 } }; throw "Visual Studio stayed busy" }
function Place($dte) {
  $f = Again { $dte.Debugger.CurrentStackFrame }
  $rip = Again { $dte.Debugger.GetExpression('@rip', $false, 2000).Value }
  "$(Again { $f.FunctionName }) $([IO.Path]::GetFileName((Again { $f.FileName }))):$(Again { $f.LineNumber }) $rip"
}
# 2: stopped in the debugger. A step that is not there at once gets five seconds (the first recording of `calls` "ended" at its first step)
function Stopped($dte, $tenths = 50) {
  for ($w = 0; $w -lt $tenths -and (Again { $dte.Debugger.CurrentMode }) -ne 2; $w++) { Start-Sleep -Milliseconds 100 }
  (Again { $dte.Debugger.CurrentMode }) -eq 2
}
function Dte {
  $where = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  $id = "VisualStudio.DTE.$((& $where -latest -property installationVersion).Split('.')[0]).0"   # the class's name carries the version
  # after many runs one that did not quit can stand in the way of a new one (the 108th start of a recording failed): end those and try again
  foreach ($try in 1..3) { try { return New-Object -ComObject $id } catch { Get-Process devenv, VsDebugConsole -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep -Seconds 10 } }
}
