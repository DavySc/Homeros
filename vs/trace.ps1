# Record what Visual Studio's debugger does on the programs RAD and cdb were recorded on (rdi/*.exe, the crashing ones left out): the reference
# the user of this debugger knows. Driven through Visual Studio's automation object (EnvDTE), with its settings as installed.
# usage: trace.ps1 <subject.exe> <out dir> <most steps>
# Two scripts per subject, as RAD's `into` and `over`. A line per step: STEP <kind> <function> <file>:<line> <rip>.
# A third, `stops`: a breakpoint on each of the first six functions `into` came through, and where each run to the next stops
# (where in a function its breakpoint lies: on its first instruction, measured; and what its arguments are shown as there).
param($exe, $out, $most)
New-Item -ItemType Directory -Force $out | Out-Null
$name = [IO.Path]::GetFileNameWithoutExtension($exe)
# Visual Studio answers "busy" to a call that comes while it works: ask again
function Again($what) { for ($i = 0; $i -lt 200; $i++) { try { return & $what } catch { Start-Sleep -Milliseconds 100 } }; throw "Visual Studio stayed busy" }
function Place($dte) {
  $f = Again { $dte.Debugger.CurrentStackFrame }
  $rip = Again { $dte.Debugger.GetExpression('@rip', $false, 2000).Value }
  "$(Again { $f.FunctionName }) $([IO.Path]::GetFileName((Again { $f.FileName }))):$(Again { $f.LineNumber }) $rip"
}
foreach ($script in 'into', 'over', 'stops') {
  $where = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  $id = "VisualStudio.DTE.$((& $where -latest -property installationVersion).Split('.')[0]).0"   # the class's name carries the version
  try { $dte = New-Object -ComObject $id } catch { "VS $name.${script}: no $id ($_)"; exit 1 }
  $lines = @()
  try {
    Again { $dte.SuppressUI = $true }
    Again { $dte.ExecuteCommand('File.OpenProject', "`"$exe`"") }
    Again { $dte.Debugger.StepInto($true) }   # starts the program and stops at main's first line
    $lines += "STEP start $(Place $dte)"
    if ($script -eq 'stops') {
      Get-Content "$out/$name.into.txt" | ForEach-Object { ($_ -split ' ')[2] } | Where-Object { $_ -match '^\w+$' -and $_ -ne 'ended' } | Select-Object -Unique -First 6 |
        ForEach-Object { $f = $_; Again { $dte.Debugger.Breakpoints.Add($f) } | Out-Null; $lines += "BREAK $f" }
    }
    for ($n = 0; $n -lt $most; $n++) {
      if ($script -eq 'stops' -and $n -ge 40) { break }
      if ($script -eq 'into') { Again { $dte.Debugger.StepInto($true) } } elseif ($script -eq 'over') { Again { $dte.Debugger.StepOver($true) } } else { Again { $dte.Debugger.Go($true) } }
      # 2: stopped in the debugger. A step that is not there at once gets five seconds (the first recording of `calls` "ended" at its first step)
      for ($w = 0; $w -lt 50 -and (Again { $dte.Debugger.CurrentMode }) -ne 2; $w++) { Start-Sleep -Milliseconds 100 }
      if ((Again { $dte.Debugger.CurrentMode }) -ne 2) { $lines += "STEP $script ended"; break }
      $shown = ''   # at a function's breakpoint: its arguments as Visual Studio shows them there (the code that makes the frame has not run yet)
      if ($script -eq 'stops') { $shown = ' ARGS ' + ((Again { @($dte.Debugger.CurrentStackFrame.Arguments | ForEach-Object { "$($_.Name)=$($_.Value)" }) }) -join ' ') }
      $lines += "STEP $script $(Place $dte)$shown"
    }
  } catch { $lines += "FAILED: $_" }
  try { $dte.Debugger.Stop($true) } catch {}
  try { $dte.Quit() } catch {}
  Start-Sleep -Seconds 5
  $lines | Set-Content "$out/$name.$script.txt"
  "VS $name.$script steps=$($lines.Count) last=[$($lines[-1])]"
}
