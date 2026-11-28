# What Visual Studio does where tests/pg.sh takes our debugger: attached to a PostgreSQL backend that sleeps, a breakpoint on
# ExecutorRun, and from the stop there the same steps. usage: pg.ps1 <process id> <out file>
param($procid, $out)
. "$PSScriptRoot/dte.ps1"
$dte = Dte
if (-not $dte) { "VS pg: no Visual Studio"; exit 1 }
$lines = @()
try {
  Again { $dte.SuppressUI = $true }
  # the breakpoint is set with the program stopped, as trace.ps1 does: one set before attaching never stopped it (the first try).
  # The program's source has to be where its build left it: stopped in code without it, Visual Studio asks for the file and is "busy" from then on
  $p = Again { @($dte.Debugger.LocalProcesses | Where-Object { $_.ProcessID -eq $procid })[0] }
  Again { $p.Attach() }
  Again { $dte.Debugger.Break($true) }
  if (-not (Stopped $dte)) { throw "no stop after attaching" }
  Again { $dte.Debugger.Breakpoints.Add('ExecutorRun') } | Out-Null
  Again { $dte.Debugger.Go($false) }
  Start-Sleep -Seconds 2   # it runs again before the stop is waited for
  if (-not (Stopped $dte 900)) { throw "no stop at the breakpoint" }
  $lines += "STEP stop $(Place $dte)"
  $lines += 'STACK ' + ((Again { @($dte.Debugger.CurrentThread.StackFrames | ForEach-Object { $_.FunctionName }) }) -join ' ')
  foreach ($e in 'queryDesc->operation', 'direction', 'count', 'execute_once', 'queryDesc->sourceText', '*queryDesc') { $lines += "VALUE $e = $((Again { $dte.Debugger.GetExpression($e, $false, 2000) }).Value)" }
  foreach ($kind in 'over', 'over', 'over', 'into', 'out') {
    if ($kind -eq 'over') { Again { $dte.Debugger.StepOver($true) } } elseif ($kind -eq 'into') { Again { $dte.Debugger.StepInto($true) } } else { Again { $dte.Debugger.StepOut($true) } }
    if (-not (Stopped $dte)) { throw "the step $kind did not end" }
    $lines += "STEP $kind $(Place $dte)"
  }
  $lines += "VALUE `$ReturnValue = $((Again { $dte.Debugger.GetExpression('$ReturnValue', $false, 2000) }).Value)"
} catch { $lines += "FAILED: $_" }
try { $dte.Debugger.DetachAll() } catch {}
try { $dte.Quit() } catch {}
$lines | Set-Content $out
$lines
