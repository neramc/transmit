; Windows installer. Expects `windeployqt` to have already been run over the
; staging directory, so every Qt library and QML module is alongside the
; executable.

!include "MUI2.nsh"

; Passed in by the release workflow as /DVERSION=x.y.z. The fallback is only
; for building the installer by hand.
!ifndef VERSION
    !define VERSION "0.1.0"
!endif

Name "Transmit"
OutFile "TransmitSetup.exe"
Unicode true

InstallDir "$PROGRAMFILES64\Transmit"
InstallDirRegKey HKLM "Software\Transmit" "InstallLocation"
RequestExecutionLevel admin

!define MUI_ICON "..\..\resources\icons\transmit.ico"
!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_LICENSE "..\..\LICENSE.md"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

; What the file's Properties dialog shows. Windows wants four parts here.
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "Transmit"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "Moves a computer's environment to another one"
VIAddVersionKey "LegalCopyright" "Transmit contributors"

Section "Transmit"
    SetOutPath "$INSTDIR"
    File /r "staging\*.*"

    CreateShortCut "$SMPROGRAMS\Transmit.lnk" "$INSTDIR\transmit.exe"

    WriteRegStr HKLM "Software\Transmit" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Transmit" \
        "DisplayName" "Transmit"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Transmit" \
        "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Transmit" \
        "UninstallString" "$INSTDIR\uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Transmit" \
        "DisplayIcon" "$INSTDIR\transmit.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Transmit" \
        "Publisher" "Transmit contributors"

    WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

Section "Uninstall"
    Delete "$SMPROGRAMS\Transmit.lnk"
    RMDir /r "$INSTDIR"
    DeleteRegKey HKLM "Software\Transmit"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Transmit"
SectionEnd
