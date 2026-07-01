#ifndef GD3RD_H
#define GD3RD_H

#include "structs.h"
#include "types.h"

extern s16 plt_req[2];
extern const u8 lpr_wrdata[3];
extern const u8 lpt_seldat[4];

s32 fsOpen(REQ* req);
void fsClose(REQ* /* unused */);
u32 fsGetFileSize(u16 fnum);
u32 fsCalSectorSize(u32 size);
s32 fsCheckCommandExecuting();
s32 fsRequestFileRead(REQ* /* unused */, void* buff);
s32 fsCheckFileReaded(REQ* /* unused */);
s32 fsFileReadSync(REQ* req, void* buff);
void waitVsyncDummy();
s16 load_it_use_any_key(u16 fnum, u8 kokey, u8 group);
s32 load_it_use_any_key2(u16 fnum, void** adrs, s16* key, u8 kokey, u8 group);
s32 load_it_use_this_key(u16 fnum, s16 key);
void Init_Load_Request_Queue_1st();
void Request_LDREQ_Break();
u8 Check_LDREQ_Break();
s32 Get_LDREQ_Depth(void);
void Push_LDREQ_Queue_Player(s16 id, s16 ix);

// OPTIONAL rollback snapshot of the AFS load-request queue family, used only
// when FISTBUMP_SAVE_LDREQ=1 (OFF by default). The simulation reads the queue's
// drain state at round/continue boundaries (Check_LDREQ_Clear /
// Check_LDREQ_Queue_Player) and the netplay AFS pump also drains it out-of-band
// on per-peer wall-clock timing. The SHIPPED default keeps the queue MONOTONIC
// (never rolled back, and excluded from the checksum — see netplay.c) so a
// rollback cannot re-issue a completed load; round-transition convergence is
// handled by save-state completeness instead. Snapshot/Restore exist purely for
// A/B measurement of the rolled-back path (which reintroduces the 1.8.0 reload
// bug). The live AFS file handle is deliberately NOT snapshotted: it is real-time
// I/O, self-corrected by fsOpen's reopen, and is not a determinism gate (the sim
// consults the result flags / queue occupancy below, not the handle).
typedef struct LDREQ_Snapshot_t {
    REQ q_ldreq[16];
    u8 ldreq_result[294];
    s16 plt_req[2];
    u8 ldreq_break;
    u8 afs_io_in_progress;
} LDREQ_Snapshot_t;

void LDREQ_Snapshot(LDREQ_Snapshot_t* dst);
void LDREQ_Restore(const LDREQ_Snapshot_t* src);
void Check_LDREQ_Queue();
s32 Check_LDREQ_Clear();
s32 Check_LDREQ_Queue_Player(s16 id);
void Push_LDREQ_Queue_Direct(s16 ix, s16 id);
void Push_LDREQ_Queue_Player(s16 id, s16 ix);
void Push_LDREQ_Queue_BG(s16 ix);
s32 Check_LDREQ_Queue_BG(s16 ix);
s32 Check_LDREQ_Queue_Direct(s16 ix);

#endif
