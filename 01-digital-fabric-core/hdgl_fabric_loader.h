/*
 * hdgl_fabric_loader.h — Universal fabric payload loader
 */

#ifndef HDGL_FABRIC_LOADER_H
#define HDGL_FABRIC_LOADER_H

#include <stdint.h>
#include <stddef.h>

/* Exec status codes */
#define FABRIC_EXEC_HDGL_PARSED    0x01
#define FABRIC_EXEC_ELF_VALID      0x02
#define FABRIC_EXEC_DISK_VALID     0x03
#define FABRIC_EXEC_FRAME_INGESTED 0x04
#define FABRIC_EXEC_RAW_STORED     0xFF
#define FABRIC_EXEC_ERR            0x00

typedef struct {
    uint64_t identity;
    uint8_t  payload_type;
    uint8_t  strand;
    uint32_t chunk_count;
    uint32_t genome_fp;
    uint8_t  exec_status;
} hdgl_fabric_exec_result_t;

int      hdgl_fabric_load(const uint8_t *data, size_t len,
                           uint32_t genome_fp, int strand_count,
                           int verbose, hdgl_fabric_exec_result_t *result_out);

int      hdgl_fabric_self_load(const uint8_t *fabric_source, size_t fabric_len,
                                uint32_t genome_fp, int verbose);

size_t   hdgl_fabric_reconstruct(uint64_t identity,
                                  uint8_t *out, size_t out_max);

int      hdgl_fabric_loaded_count(void);
uint64_t hdgl_fabric_identity(int idx);
uint8_t  hdgl_fabric_type(int idx);
uint32_t hdgl_fabric_chunk_count(int idx);
const char *hdgl_fabric_type_name(uint8_t t);

#endif
