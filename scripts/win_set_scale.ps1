# 윈도우 디스플레이 배율을 권장 배율에서 Steps 단계로 (0 = 권장 100%, 1 = 125%, 2 = 150% … CI 화면 자가 테스트용).
# 설정 앱의 배율 목록과 같은 길 (SPI_SETLOGICALDPIOVERRIDE, 문서에 없음). 운영체제가 허락하지 않으면 그대로다. 결과 배율(DPI)을 출력한다.
param([int]$Steps = 0)
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class MeonDpi {
    [DllImport("user32.dll")] public static extern bool SystemParametersInfo(uint action, uint param, IntPtr pv, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr GetDC(IntPtr h);
    [DllImport("gdi32.dll")] public static extern int GetDeviceCaps(IntPtr dc, int index);
}
"@
$ok = [MeonDpi]::SystemParametersInfo(0x9F, [uint32]$Steps, [IntPtr]::Zero, 1)
Start-Sleep -Seconds 2
Write-Output "scale steps $Steps set=$ok"
