# Run in a logged-in disposable Windows desktop, rather than the SSH session.
param([Parameter(Mandatory=$true)][string]$Application,
      [Parameter(Mandatory=$true)][string]$Results)
$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $Application).Path
$results = [IO.Path]::GetFullPath($Results)
if (Test-Path -LiteralPath $results) { throw 'Results directory must be new; never reuse a personal profile' }
[IO.Directory]::CreateDirectory($results) | Out-Null
Start-Transcript -Path (Join-Path $results 'test.log') -Force
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class BambuSmokeWindow {
 public delegate bool Callback(IntPtr h, IntPtr l);
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
 [DllImport("user32.dll")] public static extern bool EnumWindows(Callback c, IntPtr l);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out Rect r);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder b, int n);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
}
'@
$profile = Join-Path $results 'profile'
[IO.Directory]::CreateDirectory($profile) | Out-Null
$config = @{ app=@{language='en_US'; region='North America'}; firstguide=@{finish=$true; privacyuse=$false}; models=@(@{model='Bambu Lab A1';vendor='BBL';nozzle_diameter='0.4'}) } | ConvertTo-Json -Depth 5
[IO.File]::WriteAllText((Join-Path $profile 'BambuStudio.conf'), $config, [Text.UTF8Encoding]::new($false))
$process = $null
try {
 $process = Start-Process -FilePath $exe -ArgumentList @('--datadir', ('"'+$profile+'"')) -PassThru -RedirectStandardOutput (Join-Path $results 'stdout.log') -RedirectStandardError (Join-Path $results 'stderr.log')
 $script:windows = [Collections.Generic.List[object]]::new()
 $deadline = [DateTime]::UtcNow.AddSeconds(75)
 do {
  Start-Sleep -Seconds 2
  $process.Refresh()
  if ($process.HasExited) { throw "Application exited: $($process.ExitCode)" }
  $script:windows.Clear()
  [BambuSmokeWindow]::EnumWindows({ param($h,$l)
   [uint32]$owner=0
   [BambuSmokeWindow]::GetWindowThreadProcessId($h,[ref]$owner) | Out-Null
   if ($owner -eq $process.Id -and [BambuSmokeWindow]::IsWindowVisible($h)) {
    $b=[Text.StringBuilder]::new(1024); [BambuSmokeWindow]::GetWindowText($h,$b,1024) | Out-Null
    $r=[BambuSmokeWindow+Rect]::new(); [BambuSmokeWindow]::GetWindowRect($h,[ref]$r) | Out-Null
    $script:windows.Add([pscustomobject]@{Handle=$h.ToInt64();Title=$b.ToString();Width=$r.Right-$r.Left;Height=$r.Bottom-$r.Top})
   }
   return $true
  },[IntPtr]::Zero) | Out-Null
  $main=@($script:windows | Where-Object { $_.Title -eq 'Untitled' -and $_.Width -gt 700 -and $_.Height -gt 400 })
 } while (!$main.Count -and [DateTime]::UtcNow -lt $deadline)
 $script:windows | ConvertTo-Json | Set-Content (Join-Path $results 'windows.json')
 if (!$main.Count) { throw 'No visible Untitled application main window' }
 Start-Sleep -Seconds 10
 $process.Refresh()
 if ($process.HasExited) { throw 'Application exited after main-window creation' }
 [IO.File]::WriteAllText((Join-Path $results 'status.txt'),'PASS native Windows full-app main window startup')
 Write-Output 'PASS native Windows full-app main window startup'
} catch {
 $_ | Out-String | Set-Content (Join-Path $results 'failure.txt')
 [IO.File]::WriteAllText((Join-Path $results 'status.txt'),'FAIL '+$_.Exception.Message)
 throw
} finally {
 try {
 $bounds=[Windows.Forms.Screen]::PrimaryScreen.Bounds
 $bitmap=[Drawing.Bitmap]::new($bounds.Width,$bounds.Height)
 $graphics=[Drawing.Graphics]::FromImage($bitmap)
 $graphics.CopyFromScreen($bounds.Location,[Drawing.Point]::Empty,$bounds.Size)
 $bitmap.Save((Join-Path $results 'startup.png'),[Drawing.Imaging.ImageFormat]::Png)
 $graphics.Dispose();$bitmap.Dispose()
 } catch { Write-Warning "Screenshot failed: $_" }
 if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id -Force }
 Stop-Transcript
}
