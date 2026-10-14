; MEON Windows 설치 프로그램 (Inno Setup 6)
; distwin.sh 가 MEON\ 폴더에 파일을 모은 뒤 iscc /DSBVERSION=x.y.z wininstaller.iss 로 만든다.
; 64비트 전용: 독립 앱 + VST3 플러그인.

#ifndef SBVERSION
  #define SBVERSION "0.0.0"
#endif

[Setup]
AppId={{465082CB-FEF8-4130-80D0-8B6471AAAA30}
AppName=Meon
AppVersion={#SBVERSION}
AppPublisher=Meon
MinVersion=6.1
WizardStyle=modern
DefaultDirName={autopf}\Meon
DefaultGroupName=Meon
UninstallDisplayIcon={app}\Meon.exe
SetupIconFile=..\images\icons\MEON.ico
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=meon-{#SBVERSION}-setup
SetupLogging=yes
DisableWelcomePage=yes
DisableDirPage=auto
DisableProgramGroupPage=yes
ShowComponentSizes=no
CloseApplications=yes

[Types]
Name: "full"; Description: "전체 설치"
Name: "custom"; Description: "사용자 지정"; Flags: iscustom

[Components]
Name: "app"; Description: "Meon 앱"; Types: full custom; Flags: fixed
Name: "vst3"; Description: "VST3 플러그인 (DAW 에서 사용)"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "바탕 화면에 바로 가기 만들기"; Components: app

[Files]
Source: "MEON\Meon.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "MEON\README.txt"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "MEON\Plugins\VST3\Meon.vst3\*"; DestDir: "{commoncf64}\VST3\Meon.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "desktop.ini"
; VST3 폴더 아이콘 (VST3 규격: PlugIn.ico + desktop.ini). desktop.ini 는 숨김·시스템, 폴더는 시스템 속성이어야 탐색기가 읽는다
Source: "MEON\Plugins\VST3\Meon.vst3\desktop.ini"; DestDir: "{commoncf64}\VST3\Meon.vst3"; Components: vst3; Flags: ignoreversion; Attribs: hidden system

[Dirs]
Name: "{commoncf64}\VST3\Meon.vst3"; Components: vst3; Attribs: system

[Icons]
Name: "{autoprograms}\Meon"; Filename: "{app}\Meon.exe"
Name: "{autodesktop}\Meon"; Filename: "{app}\Meon.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Meon.exe"; Description: "Meon 실행"; Flags: nowait postinstall skipifsilent
; 앱 안의 업데이트(/SILENT /UPDATE=1)로 설치했으면 끝난 뒤 앱을 다시 켠다 (관리자 권한이 아닌 원래 사용자로)
Filename: "{app}\Meon.exe"; Flags: nowait runasoriginaluser; Check: IsAppUpdate

[Code]
function IsAppUpdate: Boolean;
begin
  Result := ExpandConstant('{param:UPDATE|0}') = '1';
end;
