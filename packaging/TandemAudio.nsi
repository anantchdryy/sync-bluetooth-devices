Unicode true
!include "MUI2.nsh"

!ifndef PAYLOAD
  !error "Pass /DPAYLOAD=<absolute staging directory>"
!endif
!ifndef OUTPUT
  !error "Pass /DOUTPUT=<absolute installer path>"
!endif

Name "Tandem Audio"
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\Tandem Audio"
RequestExecutionLevel user
SetCompressor /SOLID lzma
Icon "..\desktop\gui\Assets\TandemAudio.ico"
UninstallIcon "..\desktop\gui\Assets\TandemAudio.ico"
VIProductVersion "0.14.0.0"
VIAddVersionKey "ProductName" "Tandem Audio"
VIAddVersionKey "FileVersion" "0.14.0"
VIAddVersionKey "CompanyName" "Tandem Audio contributors"
VIAddVersionKey "FileDescription" "Tandem Audio installer"
VIAddVersionKey "LegalCopyright" "MIT License"

!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "Tandem Audio" Main
  SetOutPath "$INSTDIR"
  File "${PAYLOAD}\TandemAudio.Desktop.exe"
  File "${PAYLOAD}\syncaudio.exe"
  File "${PAYLOAD}\LICENSE"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\Tandem Audio"
  CreateShortcut "$SMPROGRAMS\Tandem Audio\Tandem Audio.lnk" "$INSTDIR\TandemAudio.Desktop.exe"
  CreateShortcut "$SMPROGRAMS\Tandem Audio\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\TandemAudio" "DisplayName" "Tandem Audio"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\TandemAudio" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\TandemAudio" "DisplayIcon" "$INSTDIR\TandemAudio.Desktop.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\TandemAudio" "Publisher" "Tandem Audio contributors"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\TandemAudio" "DisplayVersion" "0.14.0"
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\Tandem Audio\Tandem Audio.lnk"
  Delete "$SMPROGRAMS\Tandem Audio\Uninstall.lnk"
  RMDir "$SMPROGRAMS\Tandem Audio"
  Delete "$INSTDIR\TandemAudio.Desktop.exe"
  Delete "$INSTDIR\syncaudio.exe"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\TandemAudio"
SectionEnd
