#ifndef SMB_VERIFIER_STATE_STREAM_H
#define SMB_VERIFIER_STATE_STREAM_H

#include <stdint.h>
#include <stdio.h>

#define SMB_STATE_RAM_SIZE 0x800u
#define SMB_STATE_MASK_SIZE (SMB_STATE_RAM_SIZE / 8u)
#define SMB_STATE_FLAG_NMI_RAN 0x01u /* this record executed the SMB NMI body */

typedef struct SmbStateSnapshot {
    uint8_t ram[SMB_STATE_RAM_SIZE];
    uint8_t valid_mask[SMB_STATE_MASK_SIZE];
    uint8_t gameplay_mask[SMB_STATE_MASK_SIZE];
} SmbStateSnapshot;

void smb_state_snapshot(SmbStateSnapshot *snapshot);
int smb_state_write_header(FILE *stream, const SmbStateSnapshot *snapshot);
int smb_state_write_record(FILE *stream, uint32_t video_frame, uint8_t flags,
                           uint8_t raw_movie_input0, uint8_t raw_movie_input1,
                           const SmbStateSnapshot *snapshot);

#endif
