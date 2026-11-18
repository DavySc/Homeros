# Start raddbg on the test program, send commands over its socket, record everything seen.
$ErrorActionPreference = 'Continue'
$out = Join-Path $PWD 'out'; New-Item -ItemType Directory -Force $out, "$out/logs" | Out-Null
$rad = Join-Path $PWD 'rad/build/raddbg.exe'; $mule = Join-Path $PWD 'rad/build/mule_main.exe'
function Note($s) { "$(Get-Date -Format HH:mm:ss.fff) $s" | Tee-Object -Append "$out/probe.log" }
function Shot($name) {
  Add-Type -AssemblyName System.Windows.Forms, System.Drawing
  $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
  [System.Drawing.Graphics]::FromImage($bmp).CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
  $bmp.Save("$out/$name.png")
}
function Send($text) {
  # raddbg is a windowed program: capture its output through cmd redirection
  $f = "$out/ipc-$script:n.txt"; $script:n++
  cmd /c "`"$rad`" --ipc $text > `"$f`" 2>&1"
  Note "ipc [$text] exit=$LASTEXITCODE out=[$((Get-Content $f -Raw))]"
}
$script:n = 0
Note "raddbg bytes: $((Get-Item $rad).Length)"
$p = Start-Process $rad -ArgumentList "--user:$out/probe.raddbg_user", "--project:$out/probe.raddbg_project", "--logs:$out/logs", '--auto_step', "`"$mule`"" -PassThru
Start-Sleep 20
Note "raddbg alive after 20 s: $(-not $p.HasExited)"
Note "mule processes: $((Get-Process mule_main -ErrorAction SilentlyContinue | Measure-Object).Count)"
Shot 'after-start'
foreach ($c in 'step_over_line', 'step_over_line', 'step_into_line', 'step_out', 'step_over_inst') { Send $c; Start-Sleep 2 }
Shot 'after-steps'
Send 'kill_all'; Start-Sleep 2
Note "mule processes after kill_all: $((Get-Process mule_main -ErrorAction SilentlyContinue | Measure-Object).Count)"
Send 'exit'; Start-Sleep 5
Note "raddbg exited: $($p.HasExited)"
if (-not $p.HasExited) { $p.Kill() }
Get-ChildItem $out -Recurse | Select-Object FullName, Length | Out-String | Tee-Object -Append "$out/probe.log"
exit 0
