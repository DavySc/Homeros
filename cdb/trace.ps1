# Record what cdb (the console WinDbg, Microsoft's own debugger engine) does on the programs RAD was recorded on
# (rdi/*.exe, built and committed by rad-trace): a second reference beside RAD, for the steps where RAD is wrong or alone.
# usage: trace.ps1 <cdb.exe> <subject.exe> <out dir>
# Two scripts per subject, as RAD's `into` and `over`: from main's first instruction, 600 times one source-line step.
# A line per step: STEP <kind> <symbol+offset [file @ line] (address)>; nothing more once the program has ended.
param($cdb, $exe, $out)
New-Item -ItemType Directory -Force $out | Out-Null
$name = [IO.Path]::GetFileNameWithoutExtension($exe)
$env:_NT_SYMBOL_PATH = Split-Path $exe   # the program's own PDB only: no symbol server, so the system's code has no lines to step in
foreach ($script in 'into', 'over') {
  $step = if ($script -eq 'into') { 't' } else { 'p' }
  $f = Join-Path $env:RUNNER_TEMP "cdb-$name-$script.txt"
  # l+t: steps are by source line; l-s l-l: no source text or line echo of its own in the output; .lines: line numbers loaded
  $cmds = @('.lines -e', 'l+t', "bu ${name}!main", 'g', '.printf "STEP start %y\n", @rip')
  1..600 | ForEach-Object { $cmds += $step; $cmds += ".printf `"STEP $script %y\n`", @rip" }
  $cmds += 'q'
  $cmds | Set-Content $f
  $a = & $cdb -cf $f $exe 2>&1 | Where-Object { $_ -match '^STEP ' }
  $b = & $cdb -cf $f $exe 2>&1 | Where-Object { $_ -match '^STEP ' }
  $a | Set-Content "$out/$name.$script.txt"
  "CDB $name.$script steps=$(@($a).Count) repeatable=$((($a -join "`n") -eq ($b -join "`n"))) last=[$(@($a)[-1])]"
}
