# Record what Visual Studio's debugger does on the programs RAD and cdb were recorded on (rdi/*.exe): the reference
# the user of this debugger knows. Driven through Visual Studio's automation object (EnvDTE), with its settings as installed.
# usage: trace.ps1 <subject.exe> <out dir> <most steps>
# Two scripts per subject, as RAD's `into` and `over`. A line per step: STEP <kind> <function> <file>:<line> <rip>.
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
foreach ($script in 'into', 'over') {
  $where = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  $id = "VisualStudio.DTE.$((& $where -latest -property installationVersion).Split('.')[0]).0"   # the class's name carries the version
  try { $dte = New-Object -ComObject $id } catch { "VS $name.${script}: no $id ($_)"; exit 1 }
  $lines = @()
  try {
    Again { $dte.SuppressUI = $true }
    Again { $dte.ExecuteCommand('File.OpenProject', "`"$exe`"") }
    Again { $dte.Debugger.StepInto($true) }   # starts the program and stops at main's first line
    $lines += "STEP start $(Place $dte)"
    for ($n = 0; $n -lt $most; $n++) {
      if ($script -eq 'into') { Again { $dte.Debugger.StepInto($true) } } else { Again { $dte.Debugger.StepOver($true) } }
      if ((Again { $dte.Debugger.CurrentMode }) -ne 2) { $lines += "STEP $script ended"; break }   # 2: stopped in the debugger
      $lines += "STEP $script $(Place $dte)"
    }
  } catch { $lines += "FAILED: $_" }
  try { $dte.Debugger.Stop($true) } catch {}
  try { $dte.Quit() } catch {}
  Start-Sleep -Seconds 5
  $lines | Set-Content "$out/$name.$script.txt"
  "VS $name.$script steps=$($lines.Count) last=[$($lines[-1])]"
}
