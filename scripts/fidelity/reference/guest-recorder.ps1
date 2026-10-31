param([string]$OutDir='C:\Users\Public\ExplorerFidelityPhase2\recording',[int]$X=480,[int]$Y=240,[int]$W=640,[int]$H=400,[int]$DurationMs=11000,[int]$DelayMs=9000)
$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue'
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System; using System.Drawing; using System.Drawing.Imaging; using System.Runtime.InteropServices;
public static class FrameCap {
 [DllImport("gdi32.dll")] static extern int GetDeviceCaps(IntPtr hdc,int nIndex);
 [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr hWnd);
 [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr hWnd,IntPtr hdc);
 [DllImport("user32.dll")] static extern bool SetCursorPos(int x,int y);
 [StructLayout(LayoutKind.Sequential)] struct MI{public int dx,dy;public uint mouseData,dwFlags,time;public UIntPtr extra;}
 [StructLayout(LayoutKind.Sequential)] struct KI{public ushort vk,scan;public uint flags,time;public UIntPtr extra;}
 [StructLayout(LayoutKind.Explicit)] struct U{[FieldOffset(0)] public MI mi;[FieldOffset(0)] public KI ki;}
 [StructLayout(LayoutKind.Sequential)] struct INPUT{public uint type;public U data;}
 [DllImport("user32.dll",SetLastError=true)] static extern uint SendInput(uint n,INPUT[] p,int cb);
 public static int ScreenW(){var d=GetDC(IntPtr.Zero);int v=GetDeviceCaps(d,118);ReleaseDC(IntPtr.Zero,d);return v;}
 public static int ScreenH(){var d=GetDC(IntPtr.Zero);int v=GetDeviceCaps(d,117);ReleaseDC(IntPtr.Zero,d);return v;}
 public static Bitmap Grab(int x,int y,int w,int h){var b=new Bitmap(w,h,PixelFormat.Format24bppRgb);using(var g=Graphics.FromImage(b))g.CopyFromScreen(x,y,0,0,new Size(w,h),CopyPixelOperation.SourceCopy);return b;}
 public static void Move(int x,int y){SetCursorPos(x,y);} public static void Down(){var i=new INPUT[1];i[0].type=0;i[0].data.mi.dwFlags=2;SendInput(1,i,Marshal.SizeOf(typeof(INPUT)));} public static void Up(){var i=new INPUT[1];i[0].type=0;i[0].data.mi.dwFlags=4;SendInput(1,i,Marshal.SizeOf(typeof(INPUT)));} public static void Esc(){var i=new INPUT[1];i[0].type=1;i[0].data.ki.vk=0x1B;SendInput(1,i,Marshal.SizeOf(typeof(INPUT)));i[0].data.ki.flags=2;SendInput(1,i,Marshal.SizeOf(typeof(INPUT)));}
}
'@ -ReferencedAssemblies @('System.Drawing.dll')
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
[FrameCap]::Esc(); Start-Sleep -Milliseconds 500; if($DelayMs -gt 0){Start-Sleep -Milliseconds $DelayMs}
$frames=[System.Collections.Generic.List[object]]::new(); $events=[System.Collections.Generic.List[object]]::new(); $sw=[Diagnostics.Stopwatch]::StartNew(); $next=0; $phase=0
while($sw.ElapsedMilliseconds -lt $DurationMs){
 $t=$sw.Elapsed.TotalMilliseconds
 # Six menu open/close cycles (three repetitions) and three toolbar hover/pressed cycles.
 $schedule=@(
  @{t=300;kind='move';x=650;y=274;label='menu-1-hover'},@{t=550;kind='click';x=650;y=274;label='menu-1-open'},@{t=1050;kind='esc';label='menu-1-close'},
  @{t=1700;kind='move';x=856;y=274;label='toolbar-1-hover'},@{t=2000;kind='down';x=856;y=274;label='toolbar-1-pressed'},@{t=2250;kind='up';label='toolbar-1-release'},@{t=2500;kind='esc';label='toolbar-1-menu-close'},
  @{t=2900;kind='click';x=650;y=274;label='menu-2-open'},@{t=3400;kind='esc';label='menu-2-close'},
  @{t=4100;kind='move';x=856;y=274;label='toolbar-2-hover'},@{t=4400;kind='down';x=856;y=274;label='toolbar-2-pressed'},@{t=4650;kind='up';label='toolbar-2-release'},@{t=4900;kind='esc';label='toolbar-2-menu-close'},
  @{t=5300;kind='click';x=650;y=274;label='menu-3-open'},@{t=5800;kind='esc';label='menu-3-close'},
  @{t=6500;kind='move';x=856;y=274;label='toolbar-3-hover'},@{t=6800;kind='down';x=856;y=274;label='toolbar-3-pressed'},@{t=7050;kind='up';label='toolbar-3-release'},@{t=7300;kind='esc';label='toolbar-3-menu-close'})
 if($next -lt $schedule.Count -and $t -ge $schedule[$next].t){$a=$schedule[$next]; switch($a.kind){'move'{[FrameCap]::Move($a.x,$a.y)} 'click'{[FrameCap]::Move($a.x,$a.y);[FrameCap]::Down();Start-Sleep -Milliseconds 55;[FrameCap]::Up()} 'esc'{[FrameCap]::Esc()} 'down'{[FrameCap]::Down()} 'up'{[FrameCap]::Up()}}; $events.Add([pscustomobject]@{timestamp_ms=[math]::Round($sw.Elapsed.TotalMilliseconds,3);label=$a.label;kind=$a.kind;x=$a.x;y=$a.y});$next++ }
 $bmp=[FrameCap]::Grab($X,$Y,$W,$H);$frames.Add([pscustomobject]@{timestamp_ms=[math]::Round($sw.Elapsed.TotalMilliseconds,3);bitmap=$bmp})
 # bounded wait, sampling cadence is capture-driven and timestamps preserve actual intervals
 Start-Sleep -Milliseconds 20
}
$sw.Stop(); for($i=0;$i -lt $frames.Count;$i++){ $f=$frames[$i];$f.bitmap.Save((Join-Path $OutDir ('frame_{0:D4}.png' -f $i)),[Drawing.Imaging.ImageFormat]::Png);$f.bitmap.Dispose() }
$frameMeta=for($i=0;$i -lt $frames.Count;$i++){[pscustomobject]@{file=('frame_{0:D4}.png' -f $i);timestamp_ms=$frames[$i].timestamp_ms}}
[pscustomobject]@{width=$W;height=$H;region=@($X,$Y,$W,$H);duration_ms=[math]::Round($sw.Elapsed.TotalMilliseconds,3);frames=$frameMeta;events=$events.ToArray()} | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 (Join-Path $OutDir 'guest-timeline.json')
