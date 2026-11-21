# Record what Visual Studio's debugger does on the programs RAD and cdb were recorded on (rdi/*.exe): the reference
# the user of this debugger knows. Driven through Visual Studio's automation object (EnvDTE), with its settings as installed.
# usage: trace.ps1 <subject.exe> <out dir> <most steps>
# Two scripts per subject, as RAD's `into` and `over`. A line per step: STEP <kind> <function> <file>:<line> <rip>.
param($exe, $out, $most)
New-Item -ItemType Directory -Force $out | Out-Null
$name = [IO.Path]::GetFileNameWithoutExtension($exe)
# Visual Studio answers "busy" to a call that comes while it works: ask again
function Try($what) { for ($i = 0; $i -lt 200; $i++) { try { return & $what } catch { Start-Sleep -Milliseconds 100 } }; throw "Visual Studio stayed busy" }
function Place($dte) {
  $f = Try { $dte.Debugger.CurrentStackFrame }
  $rip = Try { $dte.Debugger.GetExpression('@rip', $false, 2000).Value }
  "$(Try { $f.FunctionName }) $([IO.Path]::GetFileName((Try { $f.FileName }))):$(Try { $f.LineNumber }) $rip"
}
foreach ($script in 'into', 'over') {
  $dte = New-Object -ComObject VisualStudio.DTE.17.0
  $lines = @()
  try {
    Try { $dte.SuppressUI = $true }
    Try { $dte.ExecuteCommand('File.OpenProject', "`"$exe`"") }
    Try { $dte.Debugger.StepInto($true) }   # starts the program and stops at main's first line
    $lines += "STEP start $(Place $dte)"
    for ($n = 0; $n -lt $most; $n++) {
      if ($script -eq 'into') { Try { $dte.Debugger.StepInto($true) } } else { Try { $dte.Debugger.StepOver($true) } }
      if ((Try { $dte.Debugger.CurrentMode }) -ne 2) { $lines += "STEP $script ended"; break }   # 2: stopped in the debugger
      $lines += "STEP $script $(Place $dte)"
    }
  } catch { $lines += "FAILED: $_" }
  try { $dte.Debugger.Stop($true) } catch {}
  try { $dte.Quit() } catch {}
  Start-Sleep -Seconds 3
  $lines | Set-Content "$out/$name.$script.txt"
  "VS $name.$script steps=$($lines.Count) last=[$($lines[-1])]"
}
