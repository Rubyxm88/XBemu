; XBemu for Windows: the installer (Inno Setup 6).
;
; Installs the full package folder for this user only (no admin prompt) in
; %LOCALAPPDATA%\Programs\XBemu, with Start menu entries, an optional desktop
; shortcut and an entry in Settings > Apps with an uninstaller. The folder is
; the same as the portable zip's, so XBemu's own updater keeps working in it.
;
;   ISCC /DBuild=2 /DLabel="v0.1 build 2" /DRelease=0.1 /DSourceDir=<XBemu folder>
;        /DOutputDir=<out> xbemu\package\installer.iss
;
; Makes XBemu-win-<Build>-setup.exe.

#ifndef Build
  #define Build "0"
#endif
#ifndef Release
  #define Release "0.1"
#endif
#ifndef Label
  #define Label "v" + Release + " build " + Build
#endif
#ifndef SourceDir
  #error Pass /DSourceDir=<the full package's XBemu folder>
#endif
#ifndef VersionNumber
  #define VersionNumber Release + "." + Build + ".0"
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
; Never change: Windows knows the installed XBemu by it
AppId={{EF0E9C7E-B02C-4FC8-BB98-252EA2802979}
AppName=XBemu
AppVersion={#Label}
AppVerName=XBemu {#Label}
VersionInfoVersion={#VersionNumber}
AppPublisher=XBemu
AppPublisherURL=https://github.com/Rubyxm88/XBemu
AppSupportURL=https://github.com/Rubyxm88/XBemu/issues
AppUpdatesURL=https://github.com/Rubyxm88/XBemu/releases
; For this user only: no admin prompt, and a folder XBemu may write to when
; it updates itself
PrivilegesRequired=lowest
DefaultDirName={autopf}\XBemu
DisableProgramGroupPage=yes
DisableDirPage=auto
UsePreviousAppDir=yes
UninstallDisplayName=XBemu
UninstallDisplayIcon={app}\XBemu.exe
SetupIconFile=..\..\ui\icons\xbemu.ico
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Closes a running XBemu (asks first) so its files can be replaced
CloseApplications=yes
RestartApplications=no
Compression=lzma2/max
SolidCompression=yes
OutputDir={#OutputDir}
OutputBaseFilename=XBemu-win-{#Build}-setup

[Tasks]
Name: desktopicon; Description: "Put an XBemu shortcut on the desktop"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Dirs]
Name: "{app}\games"

[Icons]
Name: "{autoprograms}\XBemu"; Filename: "{app}\XBemu.exe"; Comment: "Original Xbox on XB Online"
Name: "{autoprograms}\XBemu Games folder"; Filename: "{app}\games"; Comment: "Put your game disc images (.iso) here"
Name: "{autodesktop}\XBemu"; Filename: "{app}\XBemu.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\XBemu.exe"; Description: "Start XBemu now"; Flags: nowait postinstall skipifsilent
Filename: "{app}\games"; Description: "Open the games folder (put your .iso files here)"; Flags: nowait postinstall skipifsilent shellexec unchecked

[Code]
// The console, sign-in, saves, settings and games were made after the
// install, so the uninstaller leaves them unless the player says otherwise.
// A silent uninstall always keeps them.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Dir, Gap: String;
begin
  if CurUninstallStep = usPostUninstall then begin
    Dir := ExpandConstant('{app}');
    Gap := #13#10 + #13#10;
    if DirExists(Dir) and not UninstallSilent then begin
      if MsgBox('XBemu is removed.' + Gap +
                'Also delete your XBemu console (Xbox Live sign-in, saves, ' +
                'settings) and the games folder?' + Gap +
                'Choose No to keep them: installing XBemu again finds them.' + Gap +
                Dir, mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
        DelTree(Dir, True, True, True);
    end;
  end;
end;
