# Record what the real RAD Debugger does on a subject program, one line per step.
# usage: rad-trace.ps1 <raddbg.exe> <subject.exe> <out dir>
# Three scripts per subject: `into` and `over` repeat one step kind until the program ends,
# `mix` draws step kinds from a fixed generator. Each script runs twice; a script whose two runs
# differ is reported, because a trace that is not repeatable cannot be an oracle.
param($rad, $exe, $out)
$ErrorActionPreference = 'Continue'
New-Item -ItemType Directory -Force $out | Out-Null
$tmp = Join-Path $env:RUNNER_TEMP 'radtrace'; New-Item -ItemType Directory -Force $tmp | Out-Null
$name = [IO.Path]::GetFileNameWithoutExtension($exe)
function Send($text) { $f = "$tmp/ipc.txt"; cmd /c "`"$rad`" --ipc $text > `"$f`" 2>&1"; Get-Content $f -Raw }
function Field($s, $n) { if ($s -match "(?m)^\s*${n}:\s*(.*)$") { $Matches[1].Trim().Trim('"') } else { '' } }
function Stopped($minStops) {
  for ($i = 0; $i -lt 200; $i++) {
    $s = Send 'state'
    if ((Field $s 'running') -eq '0' -and ([int]('0' + (Field $s 'stop_count')) -ge $minStops -or (Place $s) -eq 'ended')) { return $s }
    Start-Sleep -Milliseconds 50
  }
  return $null
}
# Not named Where: that is PowerShell's alias for Where-Object, and an alias wins over a function.
function Place($s) {
  $m = [IO.Path]::GetFileName((Field $s 'ip_module')); $sym = Field $s 'ip_voff_symbol'; $line = Field $s 'line_num'
  if ($s -notmatch '(?s)threads:\s*\{\s*\{') { return 'ended' }
  if ($m -ne "$name.exe") { return "outside $m!$sym" }
  return "$sym $(Field $s 'file_name'):$line +$(Field $s 'ip_voff') [$(Field $s 'explanation')]"
}
function Trace($script, $cap) {
  $p = Start-Process $rad -ArgumentList "--user:$tmp/u.raddbg_user", "--project:$tmp/p.raddbg_project", "--logs:$tmp", '--auto_step', "`"$exe`"" -PassThru
  $lines = @(); $s = $null
  for ($i = 0; $i -lt 100 -and -not $s; $i++) { Start-Sleep -Milliseconds 300; $s = Stopped 1 }
  if (-not $s) { $lines += 'FAILED to reach the entry stop' } else {
    $lines += "start -> $(Place $s)"; $stops = [int](Field $s 'stop_count'); $seed = 12345
    for ($n = 1; $n -le $cap; $n++) {
      $kind = switch ($script) {
        'into' { 'step_into_line' } 'over' { 'step_over_line' }
        default { $seed = ($seed * 1103515245 + 12345) % 2147483648; @('step_into_line', 'step_into_line', 'step_over_line', 'step_over_line', 'step_out', 'step_over_inst', 'step_into_inst')[[int]([math]::Floor($seed / 65536)) % 7] }
      }
      Send $kind | Out-Null; $stops++; $s = Stopped $stops
      if (-not $s) { $lines += "$kind -> NO STOP within 10 s"; break }
      $w = Place $s; $lines += "$kind -> $w"
      if ($w -eq 'ended' -or $w -like 'outside*') { break }
    }
  }
  Send 'kill_all' | Out-Null; Send 'exit' | Out-Null
  if (-not $p.WaitForExit(10000)) { $p.Kill() }
  Remove-Item "$tmp/*" -Force -ErrorAction SilentlyContinue
  return $lines
}
foreach ($script in 'into', 'over', 'mix') {
  $a = Trace $script 600; $b = Trace $script 600
  $same = (($a -join "`n") -eq ($b -join "`n"))
  $a | Set-Content "$out/$name.$script.txt"
  if (-not $same) { $b | Set-Content "$out/$name.$script.second-run.txt" }
  # a line with no place in it means the recorder is broken, not the debugger
  if ($a | Where-Object { $_ -match '-> $' }) { "TRACE $name.$script has empty lines"; $script:broken = $true }
  "TRACE $name.$script steps=$($a.Count - 1) repeatable=$same last=[$($a[-1])]"
}
if ($script:broken) { exit 1 }
