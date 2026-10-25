; makensis /DAPPDIR=<cmake-install-dir> /DOUTFILE=<asset-path> windows.nsi
Unicode true
!include "MUI2.nsh"
!include "FileFunc.nsh"
Var IsUpdate
!ifndef APPDIR
!error "APPDIR must contain the installed application tree"
!endif
!ifndef OUTFILE
!define OUTFILE "BambuStudio_windows-x86_64.exe"
!endif
Name "Bambu Studio Updater Fork"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\Bambu Studio Updater"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
!insertmacro MUI_PAGE_WELCOME
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipUpdateDirectory
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
Function .onInit
  ${GetParameters} $R0
  ClearErrors
  ${GetOptions} $R0 "/UPDATE" $R1
  IfErrors done
  StrCpy $IsUpdate "1"
  done:
FunctionEnd
Function SkipUpdateDirectory
  StrCmp $IsUpdate "1" 0 done
  Abort
  done:
FunctionEnd
Section "Bambu Studio"
  SetOutPath "$INSTDIR"
  File /r "${APPDIR}\*"
  WriteUninstaller "$INSTDIR\uninstall-updater-fork.exe"
  CreateShortcut "$SMPROGRAMS\Bambu Studio Updater.lnk" "$INSTDIR\bambu-studio.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\BambuStudioUpdaterFork" "DisplayName" "Bambu Studio Updater Fork"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\BambuStudioUpdaterFork" "UninstallString" '$\"$INSTDIR\uninstall-updater-fork.exe$\"'
SectionEnd
Section "Uninstall"
  Delete "$SMPROGRAMS\Bambu Studio Updater.lnk"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\BambuStudioUpdaterFork"
  RMDir /r "$INSTDIR"
SectionEnd
