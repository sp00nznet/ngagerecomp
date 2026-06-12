/*
 * NGageRuntime — Symbian High-Level Emulation (HLE).
 *
 * Each Symbian import the game uses is a slot in the image's import address table.
 * ngage_hle_init() makes every slot point to itself and registers that address with
 * the dispatch table, so the game's `ldr r12,[slot]; blx r12` lands in a native shim.
 *
 * Shim ABI (ARM EABI / Symbian APCS): integer args in r0..r3, `this` in r0 for C++
 * methods, return value in r0 (r0:r1 for 64-bit). A shim reads/writes the cpu context.
 */
#ifndef NGAGE_HLE_H
#define NGAGE_HLE_H

#include "ngage_cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Wire the import address table -> shims. Generated (gen_hle.py) from imports.json. */
void ngage_hle_init(ngage_cpu_t* c);

/* ---- implemented shims (hand-written, runtime/src/hle/) ---- */
/* mem */
void hle_memcpy(ngage_cpu_t*);      /* memcpy(dst, src, n) -> dst                        */
void hle_memset(ngage_cpu_t*);      /* memset(dst, c, n)   -> dst                        */
void hle_Mem_FillZ(ngage_cpu_t*);   /* Mem::FillZ(void* p, TInt len)                     */
/* heap / new / delete */
void hle_CBase_new(ngage_cpu_t*);   /* CBase::operator new(TUint) -> zeroed              */
void hle_CBase_newL(ngage_cpu_t*);  /* CBase::operator new(TUint, TLeave) -> zeroed,leave*/
void hle_User_AllocL(ngage_cpu_t*); /* User::AllocL(TInt) -> alloc, leave on OOM         */
void hle_vec_new(ngage_cpu_t*);     /* operator new[](TUint)                             */
void hle_delete(ngage_cpu_t*);      /* operator delete / delete[]                        */
/* leave / cleanup / lifecycle */
void hle_User_LeaveIfError(ngage_cpu_t*);   /* User::LeaveIfError(TInt)                  */
void hle_TTrap_Trap(ngage_cpu_t*);          /* TTrap::Trap(TInt&) — see kernel.c note    */
void hle_TTrap_UnTrap(ngage_cpu_t*);        /* TTrap::UnTrap()                           */
void hle_Cleanup_PushL(ngage_cpu_t*);       /* CleanupStack::PushL(CBase*)               */
void hle_Cleanup_Pop(ngage_cpu_t*);         /* CleanupStack::Pop()                       */
void hle_Cleanup_PopAndDestroy(ngage_cpu_t*);   /* CleanupStack::PopAndDestroy()         */
void hle_Cleanup_PopAndDestroyN(ngage_cpu_t*);  /* CleanupStack::PopAndDestroy(TInt)     */
void hle_User_Panic(ngage_cpu_t*);          /* User::Panic(const TDesC16&, TInt)         */
void hle_User_Exit(ngage_cpu_t*);           /* User::Exit(TInt)                          */
void hle_RHandleBase_Close(ngage_cpu_t*);   /* RHandleBase::Close() (in hle/efsrv.c)     */
/* EFSRV — file server (hle/efsrv.c) */
void hle_RFs_Connect(ngage_cpu_t*);         /* RFs::Connect(TInt)                        */
void hle_RFsBase_Close(ngage_cpu_t*);       /* RFsBase::Close()                          */
void hle_RFs_Delete(ngage_cpu_t*);          /* RFs::Delete(const TDesC16&)               */
void hle_RFs_MkDir(ngage_cpu_t*);           /* RFs::MkDir(const TDesC16&)                */
void hle_RFile_Open(ngage_cpu_t*);          /* RFile::Open(RFs&, const TDesC16&, TUint)  */
void hle_RFile_Create(ngage_cpu_t*);        /* RFile::Create(RFs&, const TDesC16&, TUint)*/
void hle_RFile_Read(ngage_cpu_t*);          /* RFile::Read(TDes8&)                       */
void hle_RFile_ReadLen(ngage_cpu_t*);       /* RFile::Read(TDes8&, TInt)                 */
void hle_RFile_Write(ngage_cpu_t*);         /* RFile::Write(const TDesC8&, TInt)         */
void hle_RFile_Size(ngage_cpu_t*);          /* RFile::Size(TInt&)                        */
void hle_RFile_Seek(ngage_cpu_t*);          /* RFile::Seek(TSeek, TInt&)                 */
void hle_RFile_SetSize(ngage_cpu_t*);       /* RFile::SetSize(TInt)                      */
void hle_RFile_Flush(ngage_cpu_t*);         /* RFile::Flush()                            */
/* FBSCLI / BITGDI / NOKIAFC — graphics (hle/fbserv.c) */
void hle_CFbsBitmap_ctor(ngage_cpu_t*);         /* CFbsBitmap::CFbsBitmap()              */
void hle_CFbsBitmap_Create(ngage_cpu_t*);       /* Create(const TSize&, TDisplayMode)    */
void hle_CFbsBitmap_DataAddress(ngage_cpu_t*);  /* DataAddress() -> pixel buffer         */
void hle_CFbsBitmap_DisplayMode(ngage_cpu_t*);  /* DisplayMode()                         */
void hle_CFbsBitmap_SizeInPixels(ngage_cpu_t*); /* SizeInPixels() -> TSize               */
void hle_CFbsBitmap_Header(ngage_cpu_t*);       /* Header() -> SEpocBitmapHeader         */
void hle_RFbsSession_Connect(ngage_cpu_t*);     /* RFbsSession::Connect() (ord 156)      */
void hle_CFbsDevice_CreateContext(ngage_cpu_t*);/* CFbsDevice::CreateContext(CFbsBitGc*&)*/
void hle_BitGc_noop(ngage_cpu_t*);              /* Activate/SetDither/SetShadow/SetMode  */
void hle_NOKIAFC_present(ngage_cpu_t*);         /* NOKIAFC ordinal 1 — full-screen flip  */
/* active scheduler / CPeriodic (hle/scheduler.c) */
void hle_CPeriodic_NewL(ngage_cpu_t*);          /* CPeriodic::NewL(TInt)                 */
void hle_CPeriodic_Start(ngage_cpu_t*);         /* CPeriodic::Start(delay, interval, cb) */
void hle_sched_noop(ngage_cpu_t*);              /* CActive/CActiveScheduler/RTimer no-ops*/
int  ngage_pump_periodics(ngage_cpu_t*, int count);  /* drive the game loop N ticks      */
int  ngage_periodic_count(void);
/* control framework (hle/coe.c) */
void hle_ApplicationRect(ngage_cpu_t*);         /* CEikAppUi::ApplicationRect() -> TRect */
/* descriptors (hle/descriptors.c) */
void hle_TPtr8_pm(ngage_cpu_t*);    void hle_TPtr8_plm(ngage_cpu_t*);
void hle_TPtr16_pm(ngage_cpu_t*);   void hle_TPtr16_plm(ngage_cpu_t*);
void hle_TPtrC16_z(ngage_cpu_t*);   void hle_TBufBase(ngage_cpu_t*);
void hle_TDes_SetLength(ngage_cpu_t*); void hle_TDes8_PtrZ(ngage_cpu_t*);
/* compiler runtime (hle/softfloat.c) */
void hle_adddf3(ngage_cpu_t*); void hle_subdf3(ngage_cpu_t*); void hle_muldf3(ngage_cpu_t*);
void hle_divdf3(ngage_cpu_t*); void hle_negdf2(ngage_cpu_t*);
void hle_addsf3(ngage_cpu_t*); void hle_subsf3(ngage_cpu_t*); void hle_mulsf3(ngage_cpu_t*);
void hle_floatsidf(ngage_cpu_t*); void hle_floatsisf(ngage_cpu_t*);
void hle_fixdfsi(ngage_cpu_t*); void hle_fixsfsi(ngage_cpu_t*);
void hle_divsi3(ngage_cpu_t*); void hle_udivsi3(ngage_cpu_t*); void hle_modsi3(ngage_cpu_t*);
void hle_cmpdf2(ngage_cpu_t*); void hle_cmpsf2(ngage_cpu_t*);

#ifdef __cplusplus
}
#endif

#endif /* NGAGE_HLE_H */
