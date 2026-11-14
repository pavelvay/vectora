!include "MUI2.nsh"

!ifndef VERSION
!define VERSION "0.1.11"
!endif
!ifndef DIST
!define DIST "dist"
!endif

Name "Vectora ${VERSION}"
OutFile "Vectora-${VERSION}-Setup.exe"
InstallDir "$PROGRAMFILES64\Vectora"
InstallDirRegKey HKLM "Software\Vectora" "Install_Dir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Russian"

Section "Vectora" SecMain
    SetOutPath "$INSTDIR"
    File /r "${DIST}\*.*"

    WriteRegStr HKLM "Software\Vectora" "Install_Dir" "$INSTDIR"
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    CreateDirectory "$SMPROGRAMS\Vectora"
    CreateShortcut "$SMPROGRAMS\Vectora\Vectora.lnk" "$INSTDIR\vectora.exe"
    CreateShortcut "$SMPROGRAMS\Vectora\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
    CreateShortcut "$DESKTOP\Vectora.lnk" "$INSTDIR\vectora.exe"
SectionEnd

Section "Uninstall"
    Delete "$INSTDIR\*.*"
    RMDir /r "$INSTDIR"
    Delete "$SMPROGRAMS\Vectora\*.*"
    RMDir "$SMPROGRAMS\Vectora"
    Delete "$DESKTOP\Vectora.lnk"
    DeleteRegKey HKLM "Software\Vectora"
SectionEnd
