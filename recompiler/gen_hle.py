"""
gen_hle.py — generate the HLE wiring for a game from its import table.

Reads imports.json (from extract_imports.py) and emits hle_generated.c:
  - ngage_hle_init(c): for every import slot, write a self-pointer into guest memory
    and register the slot address with the dispatch table.
  - implemented imports -> their hand-written shim (runtime/src/hle/*.c)
  - everything else    -> a named stub that logs the exact Symbian function when hit,
    so an unimplemented call says WHAT is missing, not just an address.

Usage:  py -3.11 gen_hle.py imports.json [hle_generated.c]
"""
import json, sys, re

# Stripped import name -> implemented shim. Grow this as shims land in runtime/src/hle/.
IMPLEMENTED = {
    # mem
    "memcpy": "hle_memcpy",
    "memset": "hle_memset",
    "FillZ__3MemPvi": "hle_Mem_FillZ",
    # heap / new / delete
    "__nw__5CBaseUi": "hle_CBase_new",
    "newL__5CBaseUi": "hle_CBase_newL",
    "AllocL__4Useri": "hle_User_AllocL",
    "__builtin_vec_new": "hle_vec_new",
    "__builtin_delete": "hle_delete",
    "__builtin_vec_delete": "hle_delete",
    # leave / cleanup / lifecycle
    "LeaveIfError__4Useri": "hle_User_LeaveIfError",
    "Trap__5TTrapRi": "hle_TTrap_Trap",
    "UnTrap__5TTrap": "hle_TTrap_UnTrap",
    "PushL__12CleanupStackP5CBase": "hle_Cleanup_PushL",
    "Pop__12CleanupStack": "hle_Cleanup_Pop",
    "PopAndDestroy__12CleanupStack": "hle_Cleanup_PopAndDestroy",
    "PopAndDestroy__12CleanupStacki": "hle_Cleanup_PopAndDestroyN",
    "Panic__4UserRC7TDesC16i": "hle_User_Panic",
    "Exit__4Useri": "hle_User_Exit",
    "Close__11RHandleBase": "hle_RHandleBase_Close",
    # EFSRV — file server
    "Connect__3RFsi": "hle_RFs_Connect",
    "Close__7RFsBase": "hle_RFsBase_Close",
    "Delete__3RFsRC7TDesC16": "hle_RFs_Delete",
    "MkDir__3RFsRC7TDesC16": "hle_RFs_MkDir",
    "Open__5RFileR3RFsRC7TDesC16Ui": "hle_RFile_Open",
    "Create__5RFileR3RFsRC7TDesC16Ui": "hle_RFile_Create",
    "Read__C5RFileR5TDes8": "hle_RFile_Read",
    "Read__C5RFileR5TDes8i": "hle_RFile_ReadLen",
    "Write__5RFileRC6TDesC8i": "hle_RFile_Write",
    "Size__C5RFileRi": "hle_RFile_Size",
    "Seek__C5RFile5TSeekRi": "hle_RFile_Seek",
    "SetSize__5RFilei": "hle_RFile_SetSize",
    "Flush__5RFile": "hle_RFile_Flush",
    # FBSCLI / BITGDI / NOKIAFC — graphics
    "__10CFbsBitmap": "hle_CFbsBitmap_ctor",
    "Create__10CFbsBitmapRC5TSize12TDisplayMode": "hle_CFbsBitmap_Create",
    "DataAddress__C10CFbsBitmap": "hle_CFbsBitmap_DataAddress",
    "DisplayMode__C10CFbsBitmap": "hle_CFbsBitmap_DisplayMode",
    "SizeInPixels__C10CFbsBitmap": "hle_CFbsBitmap_SizeInPixels",
    "Header__C10CFbsBitmap": "hle_CFbsBitmap_Header",
    "FBSCLI_156": "hle_CFbsBitmap_Load",
    "CreateContext__10CFbsDeviceRP9CFbsBitGc": "hle_CFbsDevice_CreateContext",
    "Activate__9CFbsBitGcP10CFbsDevice": "hle_BitGc_noop",
    "SetDitherOrigin__9CFbsBitGcRC6TPoint": "hle_BitGc_noop",
    "SetShadowMode__9CFbsBitGci": "hle_BitGc_noop",
    "SetUserDisplayMode__9CFbsBitGc12TDisplayMode": "hle_BitGc_noop",
    "BITGDI_170": "hle_CFbsBitmapDevice_NewL",
    "NOKIAFC_1": "hle_NOKIAFC_present",
    # active scheduler / CPeriodic / CActive / RTimer
    "NewL__9CPeriodici": "hle_CPeriodic_NewL",
    "Start__9CPeriodicG27TTimeIntervalMicroSeconds32T1G9TCallBack": "hle_CPeriodic_Start",
    "Add__16CActiveSchedulerP7CActive": "hle_sched_noop",
    "SetActive__7CActive": "hle_sched_noop",
    "Cancel__7CActive": "hle_sched_noop",
    "__7CActivei": "hle_sched_noop",
    "_._7CActive": "hle_sched_noop",
    "RunError__7CActivei": "hle_sched_noop",
    "CreateLocal__6RTimer": "hle_sched_noop",
    "After__6RTimerR14TRequestStatusG27TTimeIntervalMicroSeconds32": "hle_sched_noop",
    "Cancel__6RTimer": "hle_sched_noop",
    # control framework
    "ApplicationRect__C9CEikAppUi": "hle_ApplicationRect",
    "TickCount__4User": "hle_User_TickCount",
    "After__4UserG27TTimeIntervalMicroSeconds32": "hle_User_After",
    # descriptors
    "__5TPtr8PUci": "hle_TPtr8_pm",
    "__5TPtr8PUcii": "hle_TPtr8_plm",
    "__6TPtr16PUsi": "hle_TPtr16_pm",
    "__6TPtr16PUsii": "hle_TPtr16_plm",
    "__7TPtrC16PCUs": "hle_TPtrC16_z",
    "Ptr__C7TDesC16": "hle_TDesC_Ptr",
    "Ptr__C6TDesC8": "hle_TDesC_Ptr",
    "Length__C7TDesC16": "hle_TDesC_Length",
    "Length__C6TDesC8": "hle_TDesC_Length",
    "__9TBufBase8i": "hle_TBufBase",
    "__10TBufBase16i": "hle_TBufBase",
    "SetLength__5TDes8i": "hle_TDes_SetLength",
    "PtrZ__5TDes8": "hle_TDes8_PtrZ",
    # compiler runtime (libgcc soft-float + integer divide)
    "__adddf3": "hle_adddf3", "__subdf3": "hle_subdf3", "__muldf3": "hle_muldf3",
    "__divdf3": "hle_divdf3", "__negdf2": "hle_negdf2",
    "__addsf3": "hle_addsf3", "__subsf3": "hle_subsf3", "__mulsf3": "hle_mulsf3",
    "__floatsidf": "hle_floatsidf", "__floatsisf": "hle_floatsisf",
    "__fixdfsi": "hle_fixdfsi", "__fixsfsi": "hle_fixsfsi",
    "__divsi3": "hle_divsi3", "__udivsi3": "hle_udivsi3", "__modsi3": "hle_modsi3",
    "__gedf2": "hle_cmpdf2", "__gtdf2": "hle_cmpdf2", "__ledf2": "hle_cmpdf2",
    "__ltdf2": "hle_cmpdf2", "__nedf2": "hle_cmpdf2",
    "__gtsf2": "hle_cmpsf2", "__lesf2": "hle_cmpsf2",
    # audio output stream
    "CMdaAudioOutputStreamPadFunction__Fv": "hle_CMdaAudioOutputStream_NewL",
    "NewL__CMdaAudioOutputStreamRMMdaAudioOutputStreamCallbackPCMdaServer": "hle_CMdaAudioOutputStream_NewL",
    # active scheduler + event injection (run-loop primitive)
    "Add__16CActiveSchedulerP7CActive": "hle_CActiveScheduler_Add",
    "RequestComplete__4UserRP14TRequestStatusi": "hle_User_RequestComplete",
    "WaitForRequest__4UserR14TRequestStatus": "hle_User_WaitForRequest",
    "SetActive__7CActive": "hle_CActive_SetActive",
    # Snakes graphics (CFbsBitmap reused by name; CFbsScreenDevice::Update = present point)
    "Load__10CFbsBitmapRC7TDesC16li": "hle_CFbsBitmap_Load",
    "Update__16CFbsScreenDevice": "hle_NOKIAFC_present",
    "NewL__16CFbsBitmapDeviceP10CFbsBitmap": "hle_CFbsBitmapDevice_NewL",
    "Create__9CWsBitmapRC5TSize12TDisplayMode": "hle_CFbsBitmap_Create",
    "__9CWsBitmapR10RWsSession": "hle_CFbsBitmap_ctor",
    # WS32 window server (Snakes)
    "WS32_348": "hle_ws_object",
    "WS32_58": "hle_ws_noop", "WS32_245": "hle_ws_noop", "WS32_289": "hle_ws_noop", "WS32_350": "hle_ws_noop",
    # CONE/EIKCORE singleton getters -> a valid object
    "Static__7CCoeEnv": "hle_return_object",
    "Application__C9CEikAppUi": "hle_return_object",
}


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "imports.json"
    out = sys.argv[2] if len(sys.argv) > 2 else "hle_generated.c"
    imps = json.load(open(src))

    lines = ['#include "ngage_cpu.h"', '#include "ngage_runtime.h"',
             '#include "ngage_hle.h"', '']
    stubs, regs, n_impl = [], [], 0
    for i, imp in enumerate(imps):
        name = imp["name"]
        key = name[6:] if name.startswith("__imp_") else name   # strip __imp_
        slot = imp["slot"]
        shim = IMPLEMENTED.get(key)
        if shim:
            n_impl += 1
        else:
            fn = f"hle_stub_{i}"
            label = re.sub(r'["\\]', "", f'{imp["dll"]}:{name}')
            # Default return is 0 (KErrNone / null) — correct "feature absent" semantics
            # and avoids garbage-in-r0 from the previous call corrupting a branch.
            stubs.append(f'static void {fn}(ngage_cpu_t* c){{ '
                         f'ngage_unimplemented(c, {slot:#x}u, "{label}"); c->r[0]=0; }}')
            shim = fn
        regs.append((slot, shim))

    lines += stubs
    lines += ['', '/* Point each import slot at itself and route it to a shim. */',
              'void ngage_hle_init(ngage_cpu_t* c) {']
    for slot, shim in regs:
        lines.append(f'    ngage_w32(c, {slot:#x}u, {slot:#x}u); '
                     f'ngage_register({slot:#x}u, {shim});')
    lines.append('}')

    open(out, "w").write("\n".join(lines) + "\n")
    print(f"[*] {out}: {len(imps)} imports  ({n_impl} implemented, {len(stubs)} stubbed)",
          file=sys.stderr)


if __name__ == "__main__":
    main()
