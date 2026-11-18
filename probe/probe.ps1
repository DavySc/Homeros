# Start raddbg on its test program, step it through the socket, and record `state` after every step.
# `state` (RD_CmdKind_State, raddbg_core.c) reports running, stop_count, ip, symbol, the stop event,
# locals, lines, threads, modules and breakpoints, so each step's landing place is read back as text.
$ErrorActionPreference = 'Continue'
$out = Join-Path $PWD 'out'; New-Item -ItemType Directory -Force $out, "$out/logs" | Out-Null
$rad = Join-Path $PWD 'rad/build/raddbg.exe'; $mule = Join-Path $PWD 'rad/build/mule_main.exe'
function Note($s) { "$(Get-Date -Format HH:mm:ss.fff) $s" | Tee-Object -Append "$out/probe.log" }
function Send($text) { $f = "$out/ipc.txt"; cmd /c "`"$rad`" --ipc $text > `"$f`" 2>&1"; Get-Content $f -Raw }
function Field($state, $name) { if ($state -match "(?m)^\s*${name}:\s*(.*)$") { $Matches[1].Trim() } else { '?' } }
function Stopped() {
  # poll until the target is stopped; 10 s is far beyond any single step on this program
  for ($i = 0; $i -lt 100; $i++) { $s = Send 'state'; if ((Field $s 'running') -eq '0') { return $s }; Start-Sleep -Milliseconds 100 }
  Note 'TIMEOUT waiting for stop'; return $s
}
$p = Start-Process $rad -ArgumentList "--user:$out/probe.raddbg_user", "--project:$out/probe.raddbg_project", "--logs:$out/logs", '--auto_step', "`"$mule`"" -PassThru
Start-Sleep 15
$s = Stopped; $s | Set-Content "$out/state-000-start.txt"
Note ("start: {0} line {1} stop_count {2} [{3}]" -f (Field $s 'ip_voff_symbol'), (Field $s 'line_num'), (Field $s 'stop_count'), (Field $s 'explanation'))
# a fixed mix of every step kind; the same list will be replayed against gdb-style oracles and our engine
$steps = @('step_over_line') * 6 + @('step_into_line') * 4 + @('step_out', 'step_over_line', 'step_into_line', 'step_into_line', 'step_over_inst', 'step_into_inst', 'step_over_line', 'step_out', 'step_out') + @('step_into_line') * 10 + @('step_over_line') * 10
$n = 0
foreach ($c in $steps) {
  $n++; Send $c | Out-Null; $s = Stopped
  $s | Set-Content ("$out/state-{0:d3}-$c.txt" -f $n)
  Note ("{0,3} {1,-15} -> {2} line {3} voff {4} stop_count {5} [{6}]" -f $n, $c, (Field $s 'ip_voff_symbol'), (Field $s 'line_num'), (Field $s 'ip_voff'), (Field $s 'stop_count'), (Field $s 'explanation'))
}
"===== full state after the last step"; $s
"===== eval"; Send 'eval 1+2*3'; Send 'eval sizeof(int)'
Send 'kill_all' | Out-Null; Start-Sleep 2
Note "test program processes after kill_all: $((Get-Process mule_main -ErrorAction SilentlyContinue | Measure-Object).Count)"
Send 'exit' | Out-Null; Start-Sleep 5
Note "raddbg exited: $($p.HasExited)"
if (-not $p.HasExited) { $p.Kill() }
exit 0
