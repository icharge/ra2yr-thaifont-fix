Unicode true
RequestExecutionLevel user
XPStyle on
SetCompressor /SOLID lzma
SetCompressorDictSize 16
ShowInstDetails show
AutoCloseWindow false
CRCCheck on

!include "MUI2.nsh"
!include "nsDialogs.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "TextFunc.nsh"
!include "x64.nsh"
!insertmacro TrimNewLines
!include "build-artifacts\installer-build.nsh"

!define PRODUCT_NAME "ThaiFontFix for Red Alert 2"
!define PRODUCT_VERSION "1.0.0"
!define PROXY_NAME "binkw32.dll"
!define BACKUP_NAME "binkw32.bin"
!define PROXY_STAGE "binkw32.dll.thaifnt-staging"
!define REMOVE_STAGE "binkw32.dll.thaifnt-removing"
!define CONFIG_NAME "thaifontfix.ini"
!define CONFIG_STAGE "thaifontfix.ini.thaifnt-staging"

; Restore is the removal path. Do not add WriteUninstaller or uninstaller pages.

Name "${PRODUCT_NAME}"
Caption "${PRODUCT_NAME} Setup"
OutFile "${OUTPUT_FILE}"
InstallDir "$PROGRAMFILES\Westwood\RA2"
VIProductVersion "${PRODUCT_VERSION}.0"
VIAddVersionKey /LANG=1033 "ProductName" "${PRODUCT_NAME}"
VIAddVersionKey /LANG=1033 "FileDescription" "ThaiFontFix installer"
VIAddVersionKey /LANG=1033 "FileVersion" "${PRODUCT_VERSION}"
VIAddVersionKey /LANG=1033 "ProductVersion" "${PRODUCT_VERSION}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "ThaiFontFix"

Var Action
Var InstallState
Var InstallStatusText
Var ActionDialog
Var InstallRadio
Var RestoreRadio
Var ProcessName
Var FailureMessage
Var BackupPath
Var BackupCount
Var OriginalHandle
Var ProxyHandle
Var ExportFile
Var ExportLine
Var ExportOrdinal
Var ExportName
Var ExportFailure
Var ElevationAttempted
Var LastSystemError
Var ConfigExists
Var RestoreRecovery

!insertmacro MUI_PAGE_WELCOME
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE DirectoryPageLeave
!insertmacro MUI_PAGE_DIRECTORY
Page custom ActionPageCreate ActionPageLeave
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function .onInit
  StrCpy $Action "install"
  StrCpy $ElevationAttempted "0"
  ${GetParameters} $0
  ${GetOptions} "$0" "/THAIFNT_ELEVATED=" $1
  IfErrors init_find_game_dir
  StrCmp $1 "1" 0 init_find_game_dir
  StrCpy $ElevationAttempted "1"
  ${GetOptions} "$0" "/THAIFNT_ACTION=" $1
  IfErrors init_done
  StrCmp $1 "restore" 0 init_done
  StrCpy $Action "restore"
  Goto init_done
init_find_game_dir:
  Call FindGameDirectory
init_done:
FunctionEnd

!macro TRY_GAME_REGISTRY ROOT KEY
  ReadRegStr $INSTDIR ${ROOT} "${KEY}" "InstallPath"
  Call NormalizeRegisteredGamePath
  StrCmp $R9 "1" find_done
!macroend

Function FindGameDirectory
  ; Prefer Westwood's install registry entries over paths relative to setup.
  ; Legacy 32-bit installers usually write their values to the WOW64 view.
  SetRegView 32
  !insertmacro TRY_GAME_REGISTRY HKLM "SOFTWARE\Westwood\Red Alert 2"
  !insertmacro TRY_GAME_REGISTRY HKLM "SOFTWARE\Westwood\Yuri's Revenge"
  !insertmacro TRY_GAME_REGISTRY HKLM "SOFTWARE\Westwood Studios\Red Alert 2"
  !insertmacro TRY_GAME_REGISTRY HKCU "SOFTWARE\Westwood\Red Alert 2"
  !insertmacro TRY_GAME_REGISTRY HKCU "SOFTWARE\Westwood\Yuri's Revenge"

  ${If} ${RunningX64}
    SetRegView 64
    !insertmacro TRY_GAME_REGISTRY HKLM "SOFTWARE\Westwood\Red Alert 2"
    !insertmacro TRY_GAME_REGISTRY HKLM "SOFTWARE\Westwood\Yuri's Revenge"
    !insertmacro TRY_GAME_REGISTRY HKLM "SOFTWARE\Westwood Studios\Red Alert 2"
    !insertmacro TRY_GAME_REGISTRY HKCU "SOFTWARE\Westwood\Red Alert 2"
    !insertmacro TRY_GAME_REGISTRY HKCU "SOFTWARE\Westwood\Yuri's Revenge"
    SetRegView 32
  ${EndIf}

  ; Steam's library location is checked after the direct game registry keys.
  ReadRegStr $0 HKLM "SOFTWARE\Valve\Steam" "InstallPath"
  StrCmp $0 "" steam_32_hkcu
  Call FindSteamGameDirectory
  StrCmp $R9 "1" find_done
steam_32_hkcu:
  ReadRegStr $0 HKCU "SOFTWARE\Valve\Steam" "SteamPath"
  StrCmp $0 "" steam_64_view
  Call FindSteamGameDirectory
  StrCmp $R9 "1" find_done

steam_64_view:
  ${If} ${RunningX64}
    SetRegView 64
    ReadRegStr $0 HKLM "SOFTWARE\Valve\Steam" "InstallPath"
    StrCmp $0 "" steam_64_hkcu
    Call FindSteamGameDirectory
    StrCmp $R9 "1" find_done
steam_64_hkcu:
    ReadRegStr $0 HKCU "SOFTWARE\Valve\Steam" "SteamPath"
    StrCmp $0 "" steam_64_done
    Call FindSteamGameDirectory
    StrCmp $R9 "1" find_done
steam_64_done:
    SetRegView 32
  ${EndIf}

  ; Only after registry discovery, search setup's parent folders for portable
  ; installs. The exact four-file test avoids picking the source subfolder.
  StrCpy $R6 "$EXEDIR"
  StrCpy $R5 "0"
find_installer_path:
  StrCpy $INSTDIR "$R6"
  Call IsGameExecutableDirectory
  StrCmp $R9 "1" find_done
  ${GetParent} "$R6" $R7
  StrCmp $R7 "" find_program_files
  StrCmp $R7 $R6 find_program_files
  StrCpy $R6 "$R7"
  IntOp $R5 $R5 + 1
  IntCmp $R5 5 find_installer_path find_installer_path find_program_files

find_program_files:
  StrCpy $INSTDIR "$PROGRAMFILES\Westwood\RA2"
  Call IsGameExecutableDirectory
  StrCmp $R9 "1" find_done
  StrCpy $INSTDIR "$PROGRAMFILES\Westwood Studios\Red Alert 2"
  Call IsGameExecutableDirectory
  StrCmp $R9 "1" find_done
  StrCpy $INSTDIR "$PROGRAMFILES\EA Games\Red Alert 2"
  Call IsGameExecutableDirectory
  StrCmp $R9 "1" find_done
  StrCpy $INSTDIR "$PROGRAMFILES\Origin Games\Red Alert 2"
  Call IsGameExecutableDirectory
  StrCmp $R9 "1" find_done
  StrCpy $INSTDIR "$PROGRAMFILES\Westwood\RA2"

find_done:
FunctionEnd

Function FindSteamGameDirectory
  StrCpy $R9 "0"
  StrCpy $INSTDIR "$0\steamapps\common\Command & Conquer Red Alert II"
  Call IsGameExecutableDirectory
  StrCmp $R9 "1" steam_game_done
  StrCpy $INSTDIR "$0\steamapps\common\Command & Conquer Red Alert 2"
  Call IsGameExecutableDirectory
  StrCmp $R9 "1" steam_game_done
  StrCpy $INSTDIR "$0\steamapps\common\Command and Conquer Red Alert II"
  Call IsGameExecutableDirectory
  StrCmp $R9 "1" steam_game_done
  StrCpy $INSTDIR "$0\steamapps\common\Red Alert 2"
  Call IsGameExecutableDirectory
  StrCmp $R9 "1" steam_game_done
  StrCpy $INSTDIR "$0\steamapps\common\Command and Conquer Red Alert II Yuri's Revenge"
  Call IsGameExecutableDirectory
steam_game_done:
FunctionEnd

Function NormalizeRegisteredGamePath
  StrCmp $INSTDIR "" registry_game_done
  Call IsGameExecutableDirectory
  StrCmp $R9 "1" registry_game_done
  ; Some Westwood installers store the executable path instead of its folder.
  ${GetParent} "$INSTDIR" $R8
  StrCmp $R8 "" registry_game_invalid
  StrCpy $INSTDIR "$R8"
  Call IsGameExecutableDirectory
  StrCmp $R9 "1" registry_game_done
registry_game_invalid:
  StrCpy $R9 "0"
registry_game_done:
FunctionEnd

Function ActionPageCreate
  nsDialogs::Create 1018
  Pop $ActionDialog
  ${If} $ActionDialog == error
    Abort
  ${EndIf}
  ${NSD_CreateLabel} 0 0 100% 34u "$InstallStatusText"
  Pop $0
  ${NSD_CreateLabel} 0 38u 100% 18u "Choose what to do:"
  Pop $0
  ${NSD_CreateRadioButton} 8u 62u 100% 18u "Install ThaiFontFix and preserve the original Bink DLL"
  Pop $InstallRadio
  ${NSD_CreateRadioButton} 8u 90u 100% 18u "Restore the original Bink DLL"
  Pop $RestoreRadio
  ${If} $InstallState == "inconsistent"
    EnableWindow $InstallRadio 0
    EnableWindow $RestoreRadio 0
  ${ElseIf} $InstallState == "interrupted_restore"
    EnableWindow $InstallRadio 0
    EnableWindow $RestoreRadio 1
  ${Else}
    EnableWindow $InstallRadio 1
    EnableWindow $RestoreRadio 1
  ${EndIf}
  ${If} $Action == "restore"
    ${NSD_SetState} $RestoreRadio ${BST_CHECKED}
  ${Else}
    ${NSD_SetState} $InstallRadio ${BST_CHECKED}
  ${EndIf}
  nsDialogs::Show
FunctionEnd

Function ActionPageLeave
  ${If} $InstallState == "inconsistent"
    MessageBox MB_ICONEXCLAMATION|MB_OK "$InstallStatusText"
    Abort
  ${EndIf}
  ${NSD_GetState} $RestoreRadio $0
  ${If} $0 == ${BST_CHECKED}
    StrCpy $Action "restore"
  ${Else}
    StrCpy $Action "install"
  ${EndIf}
FunctionEnd

Function DirectoryPageLeave
  Call IsGameDirectory
  StrCmp $R9 "1" dir_leave_ok
  MessageBox MB_ICONEXCLAMATION|MB_OK "Select the game folder containing RA2.exe, game.exe, ra2.mix, and language.mix."
  Abort
dir_leave_ok:
  Call DetectInstallState
FunctionEnd

Function DetectInstallState
  Push "$INSTDIR\${PROXY_NAME}"
  Call IsThaiFontProxy
  Pop $0
  Call CountBackups

  StrCmp $0 "1" detect_proxy_present detect_proxy_absent

detect_proxy_present:
  StrCmp $BackupCount "1" 0 detect_proxy_backup_problem
  StrCpy $InstallState "installed"
  StrCpy $Action "restore"
  StrCpy $InstallStatusText "ThaiFontFix is already installed. Restore is selected."
  Return

detect_proxy_backup_problem:
  StrCpy $InstallState "inconsistent"
  StrCpy $Action ""
  StrCpy $InstallStatusText "ThaiFontFix proxy found, but exactly one original Bink backup is required. Review the game folder before continuing."
  Return

detect_proxy_absent:
  StrCmp $BackupCount "0" 0 detect_backup_without_proxy
  IfFileExists "$INSTDIR\${PROXY_NAME}" 0 detect_missing_bink
  StrCpy $InstallState "not_installed"
  StrCpy $Action "install"
  StrCpy $InstallStatusText "ThaiFontFix is not installed. Install is selected."
  Return

detect_backup_without_proxy:
  IfFileExists "$INSTDIR\${REMOVE_STAGE}" 0 detect_inconsistent_backup
  StrCmp $BackupCount "1" 0 detect_inconsistent_backup
  Push "$INSTDIR\${REMOVE_STAGE}"
  Call IsThaiFontProxy
  Pop $0
  StrCmp $0 "1" 0 detect_inconsistent_backup
  StrCpy $InstallState "interrupted_restore"
  StrCpy $Action "restore"
  StrCpy $InstallStatusText "A previous restore stopped partway through. Restore is selected to finish it safely."
  Return

detect_inconsistent_backup:
  StrCpy $InstallState "inconsistent"
  StrCpy $Action ""
  StrCpy $InstallStatusText "A Bink backup exists without the ThaiFontFix proxy. Review the game folder before continuing."
  Return

detect_missing_bink:
  StrCpy $InstallState "inconsistent"
  StrCpy $Action ""
  StrCpy $InstallStatusText "No binkw32.dll was found in this game folder. Select the correct folder before continuing."
FunctionEnd

Function EnsureWritableGameDirectory
  ; Probe create and delete rights before touching the game DLLs. Protected
  ; folders can then be retried elevated without leaving a half-installed state.
  System::Call 'kernel32::GetCurrentProcessId() i .r0'
  StrCpy $0 "$INSTDIR\.thaifnt-write-check-$0.tmp"
  System::Call 'kernel32::CreateFileW(w "$0", i 0x40010000, i 7, p 0, i 1, i 0x04000100, p 0) p .r1 ?e'
  Pop $R2
  StrCmp $1 -1 write_check_failed
  StrCmp $1 4294967295 write_check_failed
  StrCmp $1 0 write_check_failed
  System::Call 'kernel32::CloseHandle(p r1) i .r2'
  Delete "$0"
  StrCpy $LastSystemError "0"
  Return

write_check_failed:
  StrCpy $LastSystemError $R2
  StrCmp $LastSystemError "5" 0 write_check_other_error
  StrCmp $ElevationAttempted "1" write_check_admin_still_denied write_check_elevate
write_check_elevate:
  Call ElevateInstaller
  StrCpy $FailureMessage "Windows could not grant administrator access. Sign in with an administrator account, then run setup again."
  Goto write_check_abort

write_check_admin_still_denied:
  StrCpy $FailureMessage "The selected game folder is still not writable with administrator access. Check its permissions or choose another folder."
  Goto write_check_abort

write_check_other_error:
  StrCpy $FailureMessage "The installer could not check write access to the selected game folder (Windows error $LastSystemError). Check the folder and try again."
write_check_abort:
  MessageBox MB_ICONSTOP|MB_OK "$FailureMessage"
  Abort
FunctionEnd

Function ElevateInstaller
  StrCmp $ElevationAttempted "1" elevate_return
  StrCpy $0 "/THAIFNT_ELEVATED=1 /THAIFNT_ACTION=$Action /D=$INSTDIR"
  ClearErrors
  ExecShell "runas" "$EXEPATH" "$0" SW_SHOWNORMAL
  IfErrors elevate_return
  Quit
elevate_return:
FunctionEnd

Function IsGameDirectory
  Call IsGameExecutableDirectory
FunctionEnd

Function IsGameExecutableDirectory
  ; Require all four files together in the selected game folder.
  StrCpy $R9 "0"
  IfFileExists "$INSTDIR\RA2.exe" 0 game_required_files_missing
  IfFileExists "$INSTDIR\GAME.exe" 0 game_required_files_missing
  IfFileExists "$INSTDIR\ra2.mix" 0 game_required_files_missing
  IfFileExists "$INSTDIR\language.mix" 0 game_required_files_missing
  StrCpy $R9 "1"
game_required_files_missing:
FunctionEnd

Function IsThaiFontProxy
  Exch $R8
  StrCpy $R7 "0"
  IfFileExists "$R8" 0 proxy_check_done
  System::Call 'kernel32::LoadLibraryExW(w "$R8", p 0, i 1) p .r6'
  StrCmp $6 0 proxy_check_done
  System::Call 'kernel32::GetProcAddress(p r6, m "ThaiFontFixIsActive") p .r5'
  StrCmp $5 0 proxy_check_free
  StrCpy $R7 "1"
proxy_check_free:
  System::Call 'kernel32::FreeLibrary(p r6) i .r4'
proxy_check_done:
  StrCpy $R8 $R7
  Exch $R8
FunctionEnd

Function CountBackups
  StrCpy $BackupCount "0"
  StrCpy $BackupPath ""
  IfFileExists "$INSTDIR\${BACKUP_NAME}" backup_bin_found backup_tlb_check
backup_bin_found:
  IntOp $BackupCount $BackupCount + 1
  StrCpy $BackupPath "$INSTDIR\${BACKUP_NAME}"
backup_tlb_check:
  IfFileExists "$INSTDIR\BINKW32.TLB" backup_tlb_found backup_legacy_check
backup_tlb_found:
  IntOp $BackupCount $BackupCount + 1
  StrCpy $BackupPath "$INSTDIR\BINKW32.TLB"
backup_legacy_check:
  IfFileExists "$INSTDIR\ThaiFontFixBinkReal.dll" backup_legacy_found backup_count_done
backup_legacy_found:
  IntOp $BackupCount $BackupCount + 1
  StrCpy $BackupPath "$INSTDIR\ThaiFontFixBinkReal.dll"
backup_count_done:
FunctionEnd

Function CheckGameProcesses
  StrCpy $ProcessName ""
  System::Call 'kernel32::SetLastError(i 0)'
  System::Call 'kernel32::CreateToolhelp32Snapshot(i 2, i 0) p .r0 ?e'
  Pop $R2
  StrCmp $0 -1 process_check_error
  StrCmp $0 4294967295 process_check_error
  StrCmp $0 0 process_check_error
  System::Alloc 556
  Pop $R1
  StrCmp $R1 0 process_check_alloc_error
  System::Call '*$R1(i 556)'
  System::Call 'kernel32::SetLastError(i 0)'
  System::Call 'kernel32::Process32FirstW(p r0, p R1) i .r2 ?e'
  Pop $R2
  StrCmp $2 0 process_first_failed
process_loop:
  StrCmp $2 0 process_check_done
  System::Call '*$R1(&v36, &w260 .r3)'
  System::Call 'kernel32::lstrcmpiW(w "$3", w "game.exe") i .r4'
  StrCmp $4 0 process_found
  System::Call 'kernel32::lstrcmpiW(w "$3", w "gamemd.exe") i .r4'
  StrCmp $4 0 process_found
  System::Call 'kernel32::lstrcmpiW(w "$3", w "gamemdpatch.exe") i .r4'
  StrCmp $4 0 process_found
  System::Call 'kernel32::lstrcmpiW(w "$3", w "ra2md.exe") i .r4'
  StrCmp $4 0 process_found
  System::Call 'kernel32::lstrcmpiW(w "$3", w "syringe.exe") i .r4'
  StrCmp $4 0 process_found
  ; Process32NextW expects dwSize set for each call. Clear last-error first so
  ; an end-of-list result cannot inherit an unrelated installer error code.
  System::Call '*$R1(i 556)'
  System::Call 'kernel32::SetLastError(i 0)'
  System::Call 'kernel32::Process32NextW(p r0, p R1) i .r2 ?e'
  Pop $R2
  StrCmp $2 0 process_next_failed
  Goto process_loop
process_next_failed:
  StrCmp $R2 18 process_check_done
  StrCmp $R2 0 process_check_done
  StrCpy $FailureMessage "Could not continue enumerating running processes (Windows error $R2)."
  Goto process_check_done
process_first_failed:
  StrCmp $R2 18 process_check_done
  StrCmp $R2 0 process_check_done
  StrCpy $FailureMessage "Could not start enumerating running processes (Windows error $R2)."
  Goto process_check_done
process_found:
  StrCpy $ProcessName $3
process_check_done:
  System::Free $R1
  System::Call 'kernel32::CloseHandle(p r0) i .r2'
  Return
process_check_alloc_error:
  System::Call 'kernel32::CloseHandle(p r0) i .r2'
  StrCpy $FailureMessage "Could not allocate memory to check running processes."
  Return
process_check_error:
  StrCpy $FailureMessage "Could not create a running-process snapshot (Windows error $R2)."
FunctionEnd

Function OpenImage
  ; Input: path on stack. Output: module handle on stack (0 means invalid or not x86).
  Exch $R8
  System::Call 'kernel32::LoadLibraryExW(w "$R8", p 0, i 1) p .r7'
  StrCpy $R8 $7
  Exch $R8
FunctionEnd

Function IsProxyModule
  ; Input: handle on stack. Output: 1 when the module has the ThaiFontFix marker.
  Exch $R8
  StrCpy $R7 "0"
  StrCmp $R8 0 proxy_module_done
  System::Call 'kernel32::GetProcAddress(p R8, m "ThaiFontFixIsActive") p .r6'
  StrCmp $6 0 proxy_module_done
  StrCpy $R7 "1"
proxy_module_done:
  StrCpy $R8 $R7
  Exch $R8
FunctionEnd

Function ValidateExportTables
  ; $OriginalHandle and $ProxyHandle are loaded with DONT_RESOLVE_DLL_REFERENCES.
  ; Check each expected name and ordinal independently. Comparing the returned
  ; function addresses rejects valid exports when a DLL uses aliases or
  ; forwarded exports, even though both required lookup forms are present.
  StrCpy $R0 $OriginalHandle
  StrCpy $R1 $ProxyHandle
  StrCpy $ExportFailure ""
  FileOpen $ExportFile "$PLUGINSDIR\expected-exports.txt" r
  IfErrors exports_file_error
exports_loop:
  ClearErrors
  FileRead $ExportFile $ExportLine
  IfErrors exports_done
  ${TrimNewLines} $ExportLine $ExportLine
  StrCmp $ExportLine "" exports_loop
  StrCpy $ExportOrdinal ""
  StrCpy $R8 0
exports_parse_ordinal:
  StrCpy $R7 $ExportLine 1 $R8
  StrCmp $R7 "|" exports_ordinal_parsed
  StrCpy $ExportOrdinal "$ExportOrdinal$R7"
  IntOp $R8 $R8 + 1
  Goto exports_parse_ordinal
exports_ordinal_parsed:
  IntOp $R8 $R8 + 1
  StrCpy $ExportName $ExportLine "" $R8
  IntOp $R9 $ExportOrdinal + 0

  System::Call 'kernel32::GetProcAddress(p R0, m "$ExportName") p .r2'
  StrCmp $2 0 exports_original_name_mismatch
  System::Call 'kernel32::GetProcAddress(p R0, i R9) p .r3'
  StrCmp $3 0 exports_original_ordinal_mismatch
  System::Call 'kernel32::GetProcAddress(p R1, m "$ExportName") p .r2'
  StrCmp $2 0 exports_proxy_name_mismatch
  System::Call 'kernel32::GetProcAddress(p R1, i R9) p .r3'
  StrCmp $3 0 exports_proxy_ordinal_mismatch
  Goto exports_loop

exports_done:
  FileClose $ExportFile
  StrCpy $R9 "1"
  Return
exports_mismatch:
  FileClose $ExportFile
  StrCpy $R9 "0"
  Return
exports_original_name_mismatch:
  StrCpy $ExportFailure "original Bink name $ExportName"
  Goto exports_mismatch
exports_original_ordinal_mismatch:
  StrCpy $ExportFailure "original Bink ordinal $ExportOrdinal"
  Goto exports_mismatch
exports_proxy_name_mismatch:
  StrCpy $ExportFailure "proxy name $ExportName"
  Goto exports_mismatch
exports_proxy_ordinal_mismatch:
  StrCpy $ExportFailure "proxy ordinal $ExportOrdinal"
  Goto exports_mismatch
exports_file_error:
  StrCpy $ExportFailure "could not read expected-exports.txt"
  StrCpy $R9 "0"
FunctionEnd

Function EnsureNoGameProcess
  Call CheckGameProcesses
  StrCmp $FailureMessage "" 0 process_blocked
  StrCmp $ProcessName "" process_check_ok
process_blocked:
  StrCmp $FailureMessage "" 0 process_error_message
  StrCpy $FailureMessage "Close the game and Syringe before installing or restoring. Still running: $ProcessName"
process_error_message:
  MessageBox MB_ICONEXCLAMATION|MB_OK "$FailureMessage"
  Abort
process_check_ok:
FunctionEnd

Section "ThaiFontFix"
  InitPluginsDir
  SetOutPath "$PLUGINSDIR"
  File /oname=binkw32.dll "${PROXY_FILE}"
  File /oname=thaifontfix.ini "${CONFIG_FILE}"
  File /oname=expected-exports.txt "${EXPORTS_FILE}"
  StrCpy $FailureMessage ""
  Call IsGameDirectory
  StrCmp $R9 "1" 0 invalid_game_dir
  Call EnsureNoGameProcess
  StrCmp $Action "restore" do_restore do_install

invalid_game_dir:
  MessageBox MB_ICONEXCLAMATION|MB_OK "The selected folder must contain RA2.exe, game.exe, ra2.mix, and language.mix together."
  Abort

do_install:
  Call InstallProxy
  Goto section_complete

do_restore:
  Call RestoreOriginal
section_complete:
SectionEnd

Function InstallProxy
  Push "$INSTDIR\${PROXY_NAME}"
  Call IsThaiFontProxy
  Pop $0
  StrCmp $0 "1" install_already_proxy
  Call CountBackups
  StrCmp $BackupCount "0" 0 install_backup_exists
  IfFileExists "$INSTDIR\${PROXY_NAME}" 0 install_missing_original
  IfFileExists "$INSTDIR\${PROXY_STAGE}" 0 install_check_config_stage
  ; A previous failed install can leave our staged proxy behind. Replace it
  ; only when its ThaiFontFix marker proves it is a recognized proxy.
  Push "$INSTDIR\${PROXY_STAGE}"
  Call IsThaiFontProxy
  Pop $0
  StrCmp $0 "1" 0 install_stage_exists
  ClearErrors
  Delete "$INSTDIR\${PROXY_STAGE}"
  IfFileExists "$INSTDIR\${PROXY_STAGE}" install_stage_cleanup_failed
install_check_config_stage:
  IfFileExists "$INSTDIR\${CONFIG_STAGE}" install_stage_exists
  IfFileExists "$INSTDIR\${CONFIG_NAME}" install_config_exists install_config_missing
install_config_exists:
  System::Call 'kernel32::GetFileAttributesW(w "$INSTDIR\${CONFIG_NAME}") i .r0'
  IntOp $0 $0 & 0x10
  StrCmp $0 0 0 install_config_is_dir
  StrCpy $ConfigExists "1"
  Goto install_config_ready

install_config_missing:
  StrCpy $ConfigExists "0"
install_config_ready:
  Call EnsureWritableGameDirectory
  StrCmp $ConfigExists "1" install_stage_proxy
  ClearErrors
  CopyFiles /SILENT "$PLUGINSDIR\thaifontfix.ini" "$INSTDIR\${CONFIG_STAGE}"
  IfErrors install_stage_write_failed
install_stage_proxy:
  ClearErrors
  CopyFiles /SILENT "$PLUGINSDIR\binkw32.dll" "$INSTDIR\${PROXY_STAGE}"
  IfErrors install_stage_write_failed

  Push "$INSTDIR\${PROXY_NAME}"
  Call OpenImage
  Pop $OriginalHandle
  StrCmp $OriginalHandle 0 install_original_invalid
  Push $OriginalHandle
  Call IsProxyModule
  Pop $0
  StrCmp $0 "1" install_original_is_proxy

  Push "$INSTDIR\${PROXY_STAGE}"
  Call OpenImage
  Pop $ProxyHandle
  StrCmp $ProxyHandle 0 install_proxy_invalid
  Push $ProxyHandle
  Call IsProxyModule
  Pop $0
  StrCmp $0 "1" 0 install_proxy_invalid
  Call ValidateExportTables
  StrCmp $R9 "1" 0 install_export_mismatch
  StrCpy $R0 $OriginalHandle
  System::Call 'kernel32::FreeLibrary(p R0) i .r0'
  StrCpy $R0 $ProxyHandle
  System::Call 'kernel32::FreeLibrary(p R0) i .r0'

  ClearErrors
  Rename "$INSTDIR\${PROXY_NAME}" "$INSTDIR\${BACKUP_NAME}"
  IfErrors install_rename_original_failed
  ClearErrors
  Rename "$INSTDIR\${PROXY_STAGE}" "$INSTDIR\${PROXY_NAME}"
  IfErrors install_proxy_move_failed
  Push "$INSTDIR\${PROXY_NAME}"
  Call IsThaiFontProxy
  Pop $0
  StrCmp $0 "1" 0 install_proxy_verify_failed
  IfFileExists "$INSTDIR\${CONFIG_NAME}" install_success
  ClearErrors
  Rename "$INSTDIR\${CONFIG_STAGE}" "$INSTDIR\${CONFIG_NAME}"
  IfErrors install_config_move_failed

install_success:
  MessageBox MB_ICONINFORMATION|MB_OK "ThaiFontFix was installed successfully."
  Return

install_already_proxy:
  StrCpy $FailureMessage "ThaiFontFix is already installed. Use Restore first if you want to reinstall it."
  Goto install_failure
install_backup_exists:
  StrCpy $FailureMessage "A preserved Bink backup already exists. Restore or inspect it before installing."
  Goto install_failure
install_missing_original:
  StrCpy $FailureMessage "The original binkw32.dll was not found in the selected folder."
  Goto install_failure
install_stage_exists:
  StrCpy $FailureMessage "A temporary installer file already exists. Inspect it before trying again."
  Goto install_failure
install_stage_cleanup_failed:
  StrCpy $FailureMessage "Could not remove the previous ThaiFontFix staging file. Check folder permissions and try again."
  Goto install_failure
install_config_is_dir:
  StrCpy $FailureMessage "thaifontfix.ini is a folder. Rename or remove it before installing."
  Goto install_failure
install_stage_write_failed:
  StrCpy $FailureMessage "The installer could not stage its files in the game folder. Check folder permissions and free space."
  Goto install_failure
install_original_invalid:
  StrCpy $FailureMessage "The selected binkw32.dll is not a valid 32-bit Bink DLL."
  Goto install_failure
install_original_is_proxy:
  StrCpy $FailureMessage "binkw32.dll is already a ThaiFontFix proxy, but no restorable original was found."
  Goto install_failure
install_proxy_invalid:
  StrCpy $FailureMessage "The packaged Bink proxy failed x86 validation. No game files were changed."
  Goto install_failure
install_export_mismatch:
  StrCpy $FailureMessage "Bink export validation failed: $ExportFailure. No game files were changed."
  Goto install_failure
install_rename_original_failed:
  StrCpy $FailureMessage "Could not rename binkw32.dll to binkw32.bin. Check folder permissions and close the game."
  Goto install_failure
install_proxy_move_failed:
  ClearErrors
  Rename "$INSTDIR\${BACKUP_NAME}" "$INSTDIR\${PROXY_NAME}"
  StrCpy $FailureMessage "Could not install the proxy. The original DLL was restored."
  Goto install_failure
install_proxy_verify_failed:
  Delete "$INSTDIR\${PROXY_NAME}"
  ClearErrors
  Rename "$INSTDIR\${BACKUP_NAME}" "$INSTDIR\${PROXY_NAME}"
  StrCpy $FailureMessage "The installed proxy failed validation. The original DLL was restored."
  Goto install_failure
install_config_move_failed:
  Delete "$INSTDIR\${PROXY_NAME}"
  ClearErrors
  Rename "$INSTDIR\${BACKUP_NAME}" "$INSTDIR\${PROXY_NAME}"
  StrCpy $FailureMessage "Could not create the default INI. The original DLL was restored."
install_failure:
  Delete "$INSTDIR\${PROXY_STAGE}"
  Delete "$INSTDIR\${CONFIG_STAGE}"
  MessageBox MB_ICONSTOP|MB_OK "$FailureMessage"
  Abort
FunctionEnd

Function RestoreOriginal
  StrCpy $RestoreRecovery "0"
  Push "$INSTDIR\${PROXY_NAME}"
  Call IsThaiFontProxy
  Pop $0
  StrCmp $0 "1" restore_proxy_present
  IfFileExists "$INSTDIR\${REMOVE_STAGE}" 0 restore_not_proxy
  StrCpy $RestoreRecovery "1"
  Push "$INSTDIR\${REMOVE_STAGE}"
  Call IsThaiFontProxy
  Pop $0
  StrCmp $0 "1" 0 restore_stage_invalid

restore_proxy_present:
  Call CountBackups
  StrCmp $BackupCount "1" 0 restore_backup_count
  StrCmp $RestoreRecovery "1" restore_validate_backup
  IfFileExists "$INSTDIR\${REMOVE_STAGE}" restore_stage_exists

restore_validate_backup:
  Push "$BackupPath"
  Call OpenImage
  Pop $OriginalHandle
  StrCmp $OriginalHandle 0 restore_backup_invalid
  Push $OriginalHandle
  Call IsProxyModule
  Pop $0
  StrCmp $0 "1" restore_backup_invalid
  StrCpy $R0 $OriginalHandle
  System::Call 'kernel32::FreeLibrary(p R0) i .r0'

  Call EnsureWritableGameDirectory
  StrCmp $RestoreRecovery "1" restore_recovery_move
  ClearErrors
  Rename "$INSTDIR\${PROXY_NAME}" "$INSTDIR\${REMOVE_STAGE}"
  IfErrors restore_proxy_move_failed
restore_recovery_move:
  ClearErrors
  Rename "$BackupPath" "$INSTDIR\${PROXY_NAME}"
  IfErrors 0 restore_original_rename_succeeded
  StrCmp $RestoreRecovery "1" restore_recovery_move_failed restore_original_move_failed
restore_original_rename_succeeded:
  Push "$INSTDIR\${PROXY_NAME}"
  Call OpenImage
  Pop $OriginalHandle
  StrCmp $OriginalHandle 0 restore_validation_failed
  Push $OriginalHandle
  Call IsProxyModule
  Pop $0
  StrCpy $R0 $OriginalHandle
  System::Call 'kernel32::FreeLibrary(p R0) i .r1'
  StrCmp $0 "1" restore_validation_failed
  ClearErrors
  Delete "$INSTDIR\${REMOVE_STAGE}"
  MessageBox MB_ICONINFORMATION|MB_OK "The original Bink DLL has been restored. ThaiFontFix settings were kept."
  Return

restore_not_proxy:
  StrCpy $FailureMessage "binkw32.dll is not the ThaiFontFix proxy; it was left unchanged."
  Goto restore_failure
restore_backup_count:
  StrCpy $FailureMessage "Expected exactly one preserved original Bink DLL; files were left unchanged."
  Goto restore_failure
restore_stage_exists:
  StrCpy $FailureMessage "A temporary restore file already exists. Inspect it before trying again."
  Goto restore_failure
restore_stage_invalid:
  StrCpy $FailureMessage "The temporary restore file is not the ThaiFontFix proxy. Files were left unchanged."
  Goto restore_failure
restore_backup_invalid:
  StrCpy $FailureMessage "The preserved file is not a valid original 32-bit Bink DLL; files were left unchanged."
  Goto restore_failure
restore_proxy_move_failed:
  StrCpy $FailureMessage "Could not rename binkw32.dll to its temporary restore name. Close the game and Syringe, then check folder permissions."
  Goto restore_failure
restore_original_move_failed:
  ClearErrors
  Rename "$INSTDIR\${REMOVE_STAGE}" "$INSTDIR\${PROXY_NAME}"
  StrCpy $FailureMessage "Could not restore the original Bink DLL. Setup attempted to put the proxy back; retry Restore if it remains staged."
  Goto restore_failure
restore_recovery_move_failed:
  StrCpy $FailureMessage "Could not finish the interrupted restore. The original Bink backup and staged proxy were left unchanged."
  Goto restore_failure
restore_validation_failed:
  ClearErrors
  Rename "$INSTDIR\${PROXY_NAME}" "$BackupPath"
  ClearErrors
  Rename "$INSTDIR\${REMOVE_STAGE}" "$INSTDIR\${PROXY_NAME}"
  StrCpy $FailureMessage "The restored file failed validation. The proxy was put back."
restore_failure:
  MessageBox MB_ICONSTOP|MB_OK "$FailureMessage"
  Abort
FunctionEnd

