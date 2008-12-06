# Script generated by the HM NIS Edit Script Wizard.

# HM NIS Edit Wizard helper defines
!define PRODUCT_NAME "MKVtoolnix"
!define PRODUCT_VERSION "2.4.1"
!define PRODUCT_PUBLISHER "Moritz Bunkus"
!define PRODUCT_WEB_SITE "http://www.bunkus.org/videotools/mkvtoolnix/"
!define PRODUCT_DIR_REGKEY "Software\Microsoft\Windows\CurrentVersion\App Paths\mmg.exe"
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
!define PRODUCT_UNINST_ROOT_KEY "HKLM"
!define PRODUCT_STARTMENU_REGVAL "NSIS:StartMenuDir"

!define MTX_REGKEY "Software\mkvmergeGUI"

SetCompressor /SOLID lzma

# MUI 1.67 compatible ------
!include "MUI.nsh"

# MUI Settings
!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

# Language Selection Dialog Settings
!define MUI_LANGDLL_REGISTRY_ROOT "${PRODUCT_UNINST_ROOT_KEY}"
!define MUI_LANGDLL_REGISTRY_KEY "${PRODUCT_UNINST_KEY}"
!define MUI_LANGDLL_REGISTRY_VALUENAME "NSIS:Language"

# Welcome page
!insertmacro MUI_PAGE_WELCOME
# License page
# !insertmacro MUI_PAGE_LICENSE "..\..\..\pfad\zur\lizenz\IhreLizenz.txt"
# Directory page
!insertmacro MUI_PAGE_DIRECTORY
# Start menu page
var ICONS_GROUP
!define MUI_STARTMENUPAGE_NODISABLE
!define MUI_STARTMENUPAGE_DEFAULTFOLDER "MKVtoolnix"
!define MUI_STARTMENUPAGE_REGISTRY_ROOT "${PRODUCT_UNINST_ROOT_KEY}"
!define MUI_STARTMENUPAGE_REGISTRY_KEY "${PRODUCT_UNINST_KEY}"
!define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "${PRODUCT_STARTMENU_REGVAL}"
!insertmacro MUI_PAGE_STARTMENU Application $ICONS_GROUP
# Instfiles page
!insertmacro MUI_PAGE_INSTFILES
# Finish page
!define MUI_FINISHPAGE_RUN "$INSTDIR\mmg.exe"
!insertmacro MUI_PAGE_FINISH

# Uninstaller pages
!insertmacro MUI_UNPAGE_INSTFILES

# Language files
!insertmacro MUI_LANGUAGE "English"

# Reserve files
!insertmacro MUI_RESERVEFILE_INSTALLOPTIONS

# MUI end ------

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "mkvtoolnix-unicode-${PRODUCT_VERSION}-setup.exe"
InstallDir "$PROGRAMFILES\MKVtoolnix"
InstallDirRegKey HKLM "${PRODUCT_DIR_REGKEY}" ""
ShowInstDetails show
ShowUnInstDetails show

# Add something to the PATH environment variable.
# From http://nsis.sourceforge.net/archive/viewpage.php?pageid=91

!verbose 3
!include "WinMessages.NSH"
!verbose 4

# AddToPath - Adds the given dir to the search path.
#        Input - head of the stack
#        Note - Win9x systems requires reboot

Function AddToPath
  Exch $0
  Push $1
  Push $2
  Push $3

  # don't add if the path doesn't exist
  IfFileExists $0 "" AddToPath_done

  ReadEnvStr $1 PATH
  Push "$1;"
  Push "$0;"
  Call StrStr
  Pop $2
  StrCmp $2 "" "" AddToPath_done
  Push "$1;"
  Push "$0\;"
  Call StrStr
  Pop $2
  StrCmp $2 "" "" AddToPath_done
  GetFullPathName /SHORT $3 $0
  Push "$1;"
  Push "$3;"
  Call StrStr
  Pop $2
  StrCmp $2 "" "" AddToPath_done
  Push "$1;"
  Push "$3\;"
  Call StrStr
  Pop $2
  StrCmp $2 "" "" AddToPath_done

  Call IsNT
  Pop $1
  StrCmp $1 1 AddToPath_NT
    # Not on NT
    StrCpy $1 $WINDIR 2
    FileOpen $1 "$1\autoexec.bat" a
    FileSeek $1 -1 END
    FileReadByte $1 $2
    IntCmp $2 26 0 +2 +2 # DOS EOF
      FileSeek $1 -1 END # write over EOF
    FileWrite $1 "$\r$\nSET PATH=%PATH%;$3$\r$\n"
    FileClose $1
    SetRebootFlag true
    Goto AddToPath_done

  AddToPath_NT:
    ReadRegStr $1 HKCU "Environment" "PATH"
    StrCpy $2 $1 1 -1 # copy last char
    StrCmp $2 ";" 0 +2 # if last char == ;
      StrCpy $1 $1 -1 # remove last char
    StrCmp $1 "" AddToPath_NTdoIt
      StrCpy $0 "$1;$0"
    AddToPath_NTdoIt:
      WriteRegExpandStr HKCU "Environment" "PATH" $0
      SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000

  AddToPath_done:
    Pop $3
    Pop $2
    Pop $1
    Pop $0
FunctionEnd

# RemoveFromPath - Remove a given dir from the path
#     Input: head of the stack

Function un.RemoveFromPath
  Exch $0
  Push $1
  Push $2
  Push $3
  Push $4
  Push $5
  Push $6

  IntFmt $6 "%c" 26 # DOS EOF

  Call un.IsNT
  Pop $1
  StrCmp $1 1 unRemoveFromPath_NT
    # Not on NT
    StrCpy $1 $WINDIR 2
    FileOpen $1 "$1\autoexec.bat" r
    GetTempFileName $4
    FileOpen $2 $4 w
    GetFullPathName /SHORT $0 $0
    StrCpy $0 "SET PATH=%PATH%;$0"
    Goto unRemoveFromPath_dosLoop

    unRemoveFromPath_dosLoop:
      FileRead $1 $3
      StrCpy $5 $3 1 -1 # read last char
      StrCmp $5 $6 0 +2 # if DOS EOF
        StrCpy $3 $3 -1 # remove DOS EOF so we can compare
      StrCmp $3 "$0$\r$\n" unRemoveFromPath_dosLoopRemoveLine
      StrCmp $3 "$0$\n" unRemoveFromPath_dosLoopRemoveLine
      StrCmp $3 "$0" unRemoveFromPath_dosLoopRemoveLine
      StrCmp $3 "" unRemoveFromPath_dosLoopEnd
      FileWrite $2 $3
      Goto unRemoveFromPath_dosLoop
      unRemoveFromPath_dosLoopRemoveLine:
        SetRebootFlag true
        Goto unRemoveFromPath_dosLoop

    unRemoveFromPath_dosLoopEnd:
      FileClose $2
      FileClose $1
      StrCpy $1 $WINDIR 2
      Delete "$1\autoexec.bat"
      CopyFiles /SILENT $4 "$1\autoexec.bat"
      Delete $4
      Goto unRemoveFromPath_done

  unRemoveFromPath_NT:
    ReadRegStr $1 HKCU "Environment" "PATH"
    StrCpy $5 $1 1 -1 # copy last char
    StrCmp $5 ";" +2 # if last char != ;
      StrCpy $1 "$1;" # append ;
    Push $1
    Push "$0;"
    Call un.StrStr # Find `$0;` in $1
    Pop $2 # pos of our dir
    StrCmp $2 "" unRemoveFromPath_done
      # else, it is in path
      # $0 - path to add
      # $1 - path var
      StrLen $3 "$0;"
      StrLen $4 $2
      StrCpy $5 $1 -$4 # $5 is now the part before the path to remove
      StrCpy $6 $2 "" $3 # $6 is now the part after the path to remove
      StrCpy $3 $5$6

      StrCpy $5 $3 1 -1 # copy last char
      StrCmp $5 ";" 0 +2 # if last char == ;
        StrCpy $3 $3 -1 # remove last char

      WriteRegExpandStr HKCU "Environment" "PATH" $3
      SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000

  unRemoveFromPath_done:
    Pop $6
    Pop $5
    Pop $4
    Pop $3
    Pop $2
    Pop $1
    Pop $0
FunctionEnd

###########################################
#            Utility Functions            #
###########################################

# IsNT
# no input
# output, top of the stack = 1 if NT or 0 if not
#
# Usage:
#   Call IsNT
#   Pop $R0
#  ($R0 at this point is 1 or 0)

!macro IsNT un
Function ${un}IsNT
  Push $0
  ReadRegStr $0 HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion" CurrentVersion
  StrCmp $0 "" 0 IsNT_yes
  # we are not NT.
  Pop $0
  Push 0
  Return

  IsNT_yes:
    # NT!!!
    Pop $0
    Push 1
FunctionEnd
!macroend
!insertmacro IsNT ""
!insertmacro IsNT "un."

# StrStr
# input, top of stack = string to search for
#        top of stack-1 = string to search in
# output, top of stack (replaces with the portion of the string remaining)
# modifies no other variables.
#
# Usage:
#   Push "this is a long ass string"
#   Push "ass"
#   Call StrStr
#   Pop $R0
#  ($R0 at this point is "ass string")

!macro StrStr un
Function ${un}StrStr
Exch $R1 # st=haystack,old$R1, $R1=needle
  Exch    # st=old$R1,haystack
  Exch $R2 # st=old$R1,old$R2, $R2=haystack
  Push $R3
  Push $R4
  Push $R5
  StrLen $R3 $R1
  StrCpy $R4 0
  # $R1=needle
  # $R2=haystack
  # $R3=len(needle)
  # $R4=cnt
  # $R5=tmp
  loop:
    StrCpy $R5 $R2 $R3 $R4
    StrCmp $R5 $R1 done
    StrCmp $R5 "" done
    IntOp $R4 $R4 + 1
    Goto loop
done:
  StrCpy $R1 $R2 "" $R4
  Pop $R5
  Pop $R4
  Pop $R3
  Pop $R2
  Exch $R1
FunctionEnd
!macroend
!insertmacro StrStr ""
!insertmacro StrStr "un."

Function .onInit
  # Check if we're running on a Unicode capable Windows.
  # If not, abort.
  Call IsNT
  Pop $1
  StrCmp $1 1 DontBailOut

  # Don't install on 95, 98, ME
  MessageBox MB_OK|MB_ICONSTOP "You are trying to install MKVToolNix on a Windows version that does not support Unicode (95, 98 or ME). These old Windows versions are not supported anymore. You can still get an older version (v2.2.0) for Windows 95, 98 and ME from http://www.bunkus.org/videotools/mkvtoolnix/"
  Abort

 DontBailOut:
FunctionEnd

Section "Program files" SEC01
  SetShellVarContext all

  SetOutPath "$INSTDIR"
  SetOverwrite ifnewer
  File "cygz.dll"
  File "libcharset.dll"
  File "libebml.dll"
  File "libiconv.dll"
  File "libmatroska.dll"
  File "matroskalogo_big.ico"
  File "mkvextract.exe"
  File "mkvinfo.exe"
  File "mkvmerge.exe"
  File "mmg.exe"
  File "wxbase28u_gcc_custom.dll"
  File "wxmsw28u_core_gcc_custom.dll"
  File "wxmsw28u_html_gcc_custom.dll"
  SetOutPath "$INSTDIR\doc"
  File "doc\ChangeLog.txt"
  File "doc\COPYING.txt"
  File "doc\mkvextract.html"
  File "doc\mkvinfo.html"
  File "doc\mkvmerge-gui.hhc"
  File "doc\mkvmerge-gui.hhk"
  File "doc\mkvmerge-gui.hhp"
  File "doc\mkvmerge-gui.html"
  File "doc\mkvmerge.html"
  File "doc\mmg.html"
  File "doc\README.txt"
  File "doc\README.Windows.txt"
  SetOutPath "$INSTDIR\doc\images"
  File "doc\images\addingremovingattachments.gif"
  File "doc\images\addremovefiles.gif"
  File "doc\images\attachmentoptions.gif"
  File "doc\images\audiotrackoptions.gif"
  File "doc\images\chaptereditor.gif"
  File "doc\images\generaltrackoptions.gif"
  File "doc\images\jobmanager.gif"
  File "doc\images\movietitle.gif"
  File "doc\images\muxingwindow.gif"
  File "doc\images\selectmkvmergeexecutable.gif"
  File "doc\images\splitting.gif"
  File "doc\images\textsubtitlestrackoptions.gif"
  File "doc\images\trackselection.gif"
  File "doc\images\videotrackoptions.gif"
  SetOutPath "$INSTDIR\examples"
  File "examples\example-chapters-1.xml"
  File "examples\example-chapters-2.xml"
  File "examples\example-cue-sheet-1.cue"
  File "examples\example-segmentinfo-1.xml"
  File "examples\example-tags-2.xml"
  File "examples\example-timecodes-v1.txt"
  File "examples\example-timecodes-v2.txt"
  File "examples\matroskachapters.dtd"
  File "examples\matroskasegmentinfo.dtd"
  File "examples\matroskatags.dtd"

  # Delete files that might be present from older installation
  # if this is just an upgrade.
  Delete "$INSTDIR\base64tool.exe"
  Delete "$INSTDIR\doc\base64tool.html"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\App Paths\AppMainExe.exe"
  Delete "$SMPROGRAMS\$ICONS_GROUP\Documentation\Command line reference\base64tool CLI reference.lnk"
  SetShellVarContext current
  Delete "$SMPROGRAMS\$ICONS_GROUP\Documentation\Command line reference\base64tool CLI reference.lnk"
  SetShellVarContext all

  # Shortcuts
  !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
  CreateDirectory "$SMPROGRAMS\$ICONS_GROUP"
  CreateShortCut "$SMPROGRAMS\$ICONS_GROUP\mkvmerge GUI.lnk" "$INSTDIR\mmg.exe" "" "$INSTDIR\matroskalogo_big.ico"
  CreateShortCut "$SMPROGRAMS\$ICONS_GROUP\mkvinfo GUI.lnk" "$INSTDIR\mkvinfo.exe" "-g" "$INSTDIR\matroskalogo_big.ico"
  CreateDirectory "$SMPROGRAMS\$ICONS_GROUP\Documentation"
  CreateShortCut "$SMPROGRAMS\$ICONS_GROUP\Documentation\mkvmerge GUI guide.lnk" "$INSTDIR\doc\mkvmerge-gui.html"
  CreateDirectory "$SMPROGRAMS\$ICONS_GROUP\Documentation\Command line reference"
  CreateShortCut "$SMPROGRAMS\$ICONS_GROUP\Documentation\Command line reference\mkvmerge CLI reference.lnk" "$INSTDIR\doc\mkvmerge.html"
  CreateShortCut "$SMPROGRAMS\$ICONS_GROUP\Documentation\Command line reference\mkvinfo CLI reference.lnk" "$INSTDIR\doc\mkvinfo.html"
  CreateShortCut "$SMPROGRAMS\$ICONS_GROUP\Documentation\Command line reference\mkvextract CLI reference.lnk" "$INSTDIR\doc\mkvextract.html"
  CreateDirectory "$SMPROGRAMS\$ICONS_GROUP\Documentation\Other documentation"
  CreateShortCut "$SMPROGRAMS\$ICONS_GROUP\Documentation\Other documentation\ChangeLog - What is new.lnk" "$INSTDIR\doc\ChangeLog.txt"
  CreateShortCut "$SMPROGRAMS\$ICONS_GROUP\Documentation\Other documentation\README.lnk" "$INSTDIR\doc\README.txt"
  CreateShortCut "$SMPROGRAMS\$ICONS_GROUP\Documentation\Other documentation\The GNU GPL.lnk" "$INSTDIR\doc\Copying.txt"
  CreateShortCut "$DESKTOP\mkvmerge GUI.lnk" "$INSTDIR\mmg.exe" "" "$INSTDIR\matroskalogo_big.ico"
  !insertmacro MUI_STARTMENU_WRITE_END
SectionEnd

Section -AdditionalIcons
  !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
  WriteIniStr "$INSTDIR\${PRODUCT_NAME}.url" "InternetShortcut" "URL" "${PRODUCT_WEB_SITE}"
  CreateShortCut "$SMPROGRAMS\$ICONS_GROUP\Website.lnk" "$INSTDIR\${PRODUCT_NAME}.url"
  CreateShortCut "$SMPROGRAMS\$ICONS_GROUP\Uninstall.lnk" "$INSTDIR\uninst.exe"
  !insertmacro MUI_STARTMENU_WRITE_END
SectionEnd

Section -Post
  WriteUninstaller "$INSTDIR\uninst.exe"
  WriteRegStr HKLM "${PRODUCT_DIR_REGKEY}" "" "$INSTDIR\mmg.exe"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayName" "$(^Name)"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "UninstallString" "$INSTDIR\uninst.exe"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayIcon" "$INSTDIR\AppMainExe.exe"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "URLInfoAbout" "${PRODUCT_WEB_SITE}"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
  WriteRegStr HKCU "${MTX_REGKEY}\GUI" "installation_path" "$INSTDIR"

  Push $INSTDIR
  Call AddToPath
SectionEnd

var unRemoveJobs

Function un.onUninstSuccess
  HideWindow
  MessageBox MB_ICONINFORMATION|MB_OK "$(^Name) was successfully uninstalled."
FunctionEnd

Function un.onInit
!insertmacro MUI_UNGETLANGUAGE
  MessageBox MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2 "Do you really want to remove $(^Name) and all of its components?" IDYES +2
  Abort
  StrCpy $unRemoveJobs "No"
  IfFileExists "$INSTDIR\jobs\*.*" +2
  Return
  MessageBox MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2 "Should job files created by the GUI be deleted as well?" IDYES +2
  Return
  StrCpy $unRemoveJobs "Yes"
FunctionEnd

Section Uninstall
  SetShellVarContext all

  !insertmacro MUI_STARTMENU_GETFOLDER "Application" $ICONS_GROUP
  Delete "$SMPROGRAMS\$ICONS_GROUP\Uninstall.lnk"
  Delete "$SMPROGRAMS\$ICONS_GROUP\Website.lnk"

  Delete "$SMPROGRAMS\$ICONS_GROUP\mkvmerge GUI.lnk"
  Delete "$SMPROGRAMS\$ICONS_GROUP\mkvinfo GUI.lnk"
  Delete "$SMPROGRAMS\$ICONS_GROUP\Documentation\mkvmerge GUI guide.lnk"
  Delete "$SMPROGRAMS\$ICONS_GROUP\Documentation\Command line reference\mkvmerge CLI reference.lnk"
  Delete "$SMPROGRAMS\$ICONS_GROUP\Documentation\Command line reference\mkvinfo CLI reference.lnk"
  Delete "$SMPROGRAMS\$ICONS_GROUP\Documentation\Command line reference\mkvextract CLI reference.lnk"
  Delete "$SMPROGRAMS\$ICONS_GROUP\Documentation\Other documentation\ChangeLog - What is new.lnk"
  Delete "$SMPROGRAMS\$ICONS_GROUP\Documentation\Other documentation\README.lnk"
  Delete "$SMPROGRAMS\$ICONS_GROUP\Documentation\Other documentation\The GNU GPL.lnk"

  RMDir "$SMPROGRAMS\$ICONS_GROUP\Documentation\Other documentation"
  RMDir "$SMPROGRAMS\$ICONS_GROUP\Documentation\Command line reference"
  RMDir "$SMPROGRAMS\$ICONS_GROUP\Documentation"
  RMDir "$SMPROGRAMS\$ICONS_GROUP"

  Delete "$DESKTOP\mkvmerge GUI.lnk"

  Delete "$INSTDIR\${PRODUCT_NAME}.url"
  Delete "$INSTDIR\uninst.exe"
  Delete "$INSTDIR\cygz.dll"
  Delete "$INSTDIR\libcharset.dll"
  Delete "$INSTDIR\libebml.dll"
  Delete "$INSTDIR\libiconv.dll"
  Delete "$INSTDIR\libmatroska.dll"
  Delete "$INSTDIR\matroskalogo_big.ico"
  Delete "$INSTDIR\mkvextract.exe"
  Delete "$INSTDIR\mkvinfo.exe"
  Delete "$INSTDIR\mkvmerge.exe"
  Delete "$INSTDIR\mmg.exe"
  Delete "$INSTDIR\wxbase28u_gcc_custom.dll"
  Delete "$INSTDIR\wxmsw28u_core_gcc_custom.dll"
  Delete "$INSTDIR\wxmsw28u_html_gcc_custom.dll"

  Delete "$INSTDIR\doc\ChangeLog.txt"
  Delete "$INSTDIR\doc\COPYING.txt"
  Delete "$INSTDIR\doc\mkvextract.html"
  Delete "$INSTDIR\doc\mkvinfo.html"
  Delete "$INSTDIR\doc\mkvmerge-gui.hhc"
  Delete "$INSTDIR\doc\mkvmerge-gui.hhk"
  Delete "$INSTDIR\doc\mkvmerge-gui.hhp"
  Delete "$INSTDIR\doc\mkvmerge-gui.html"
  Delete "$INSTDIR\doc\mkvmerge.html"
  Delete "$INSTDIR\doc\mmg.html"
  Delete "$INSTDIR\doc\README.txt"
  Delete "$INSTDIR\doc\README.Windows.txt"

  Delete "$INSTDIR\doc\images\addingremovingattachments.gif"
  Delete "$INSTDIR\doc\images\addremovefiles.gif"
  Delete "$INSTDIR\doc\images\attachmentoptions.gif"
  Delete "$INSTDIR\doc\images\audiotrackoptions.gif"
  Delete "$INSTDIR\doc\images\chaptereditor.gif"
  Delete "$INSTDIR\doc\images\generaltrackoptions.gif"
  Delete "$INSTDIR\doc\images\jobmanager.gif"
  Delete "$INSTDIR\doc\images\movietitle.gif"
  Delete "$INSTDIR\doc\images\muxingwindow.gif"
  Delete "$INSTDIR\doc\images\selectmkvmergeexecutable.gif"
  Delete "$INSTDIR\doc\images\splitting.gif"
  Delete "$INSTDIR\doc\images\textsubtitlestrackoptions.gif"
  Delete "$INSTDIR\doc\images\trackselection.gif"
  Delete "$INSTDIR\doc\images\videotrackoptions.gif"

  Delete "$INSTDIR\examples\example-chapters-1.xml"
  Delete "$INSTDIR\examples\example-chapters-2.xml"
  Delete "$INSTDIR\examples\example-cue-sheet-1.cue"
  Delete "$INSTDIR\examples\example-segmentinfo-1.xml"
  Delete "$INSTDIR\examples\example-tags-2.xml"
  Delete "$INSTDIR\examples\example-timecodes-v1.txt"
  Delete "$INSTDIR\examples\example-timecodes-v2.txt"
  Delete "$INSTDIR\examples\matroskachapters.dtd"
  Delete "$INSTDIR\examples\matroskasegmentinfo.dtd"
  Delete "$INSTDIR\examples\matroskatags.dtd"

  RMDir "$INSTDIR\doc\images"
  RMDir "$INSTDIR\doc"
  RMDir "$INSTDIR\examples"

  StrCmp $unRemoveJobs "Yes" 0 +3
  Delete "$INSTDIR\jobs\*.mmg"
  RMDir "$INSTDIR\jobs"

  RMDir "$INSTDIR"

  DeleteRegKey ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}"
  DeleteRegKey HKLM "${PRODUCT_DIR_REGKEY}"
  DeleteRegKey HKCU "${MTX_REGKEY}"

  Push $INSTDIR
  Call un.RemoveFromPath

  SetAutoClose true
SectionEnd
