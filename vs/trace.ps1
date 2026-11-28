# Record what Visual Studio's debugger does on the programs RAD and cdb were recorded on (rdi/*.exe, the crashing ones left out): the reference
# the user of this debugger knows. Driven through Visual Studio's automation object (EnvDTE), with its settings as installed.
# usage: trace.ps1 <subject.exe> <out dir> <most steps>
# Two scripts per subject, as RAD's `into` and `over`. A line per step: STEP <kind> <function> <file>:<line> <rip>.
# A third, `stops`: a breakpoint on each of the first six functions `into` came through, and where each run to the next stops
# (where in a function its breakpoint lies: on its first instruction, measured; and what its arguments are shown as there).
# A fourth, `outs`: the same breakpoints, and from each a step out: where it stops and what Visual Studio says the function returned.
param($exe, $out, $most)
New-Item -ItemType Directory -Force $out | Out-Null
$name = [IO.Path]::GetFileNameWithoutExtension($exe)
. "$PSScriptRoot/dte.ps1"
foreach ($script in 'into', 'over', 'stops', 'outs') {
  $dte = Dte
  if (-not $dte) { "VS $name.${script}: no Visual Studio"; exit 1 }
  $lines = @()
  try {
    Again { $dte.SuppressUI = $true }
    Again { $dte.ExecuteCommand('File.OpenProject', "`"$exe`"") }
    Again { $dte.Debugger.StepInto($true) }   # starts the program and stops at main's first line
    $lines += "STEP start $(Place $dte)"
    if ($script -in 'stops', 'outs') {
      Get-Content "$out/$name.into.txt" | ForEach-Object { ($_ -split ' ')[2] } | Where-Object { $_ -match '^\w+$' -and $_ -ne 'ended' } | Select-Object -Unique -First 6 |
        ForEach-Object { $f = $_; Again { $dte.Debugger.Breakpoints.Add($f) } | Out-Null; $lines += "BREAK $f" }
    }
    for ($n = 0; $n -lt $most; $n++) {
      if ($script -in 'stops', 'outs' -and $n -ge 40) { break }
      if ($script -eq 'into') { Again { $dte.Debugger.StepInto($true) } } elseif ($script -eq 'over') { Again { $dte.Debugger.StepOver($true) } } else { Again { $dte.Debugger.Go($true) } }
      if (-not (Stopped $dte)) { $lines += "STEP $script ended"; break }
      $shown = ''   # at a function's breakpoint: its arguments as Visual Studio shows them there (the code that makes the frame has not run yet)
      if ($script -eq 'stops') { $shown = ' ARGS ' + ((Again { @($dte.Debugger.CurrentStackFrame.Arguments | ForEach-Object { "$($_.Name)=$($_.Value)" }) }) -join ' ') }
      if ($script -eq 'outs') {
        $from = Again { $dte.Debugger.CurrentStackFrame.FunctionName }
        Again { $dte.Debugger.StepOut($true) }
        if (-not (Stopped $dte)) { $lines += "STEP $script ended"; break }
        $r = Again { $dte.Debugger.GetExpression('$ReturnValue', $false, 2000) }
        $shown = " FROM $from RET $($r.IsValidValue) $($r.Type)=$($r.Value)"
      }
      $lines += "STEP $script $(Place $dte)$shown"
    }
  } catch { $lines += "FAILED: $_" }
  try { $dte.Debugger.Stop($true) } catch {}
  try { $dte.Quit() } catch {}
  Start-Sleep -Seconds 5
  # Visual Studio now and then loses a program early ("ended" after a few steps, another program each run): the longer recording stays
  if (-not (Test-Path "$out/$name.$script.txt") -or $lines.Count -ge @(Get-Content "$out/$name.$script.txt").Count) { $lines | Set-Content "$out/$name.$script.txt" }
  "VS $name.$script steps=$($lines.Count) last=[$($lines[-1])]"
}
