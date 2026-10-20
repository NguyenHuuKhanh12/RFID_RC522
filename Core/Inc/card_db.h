#ifndef CARD_DB_H
#define CARD_DB_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/* Vung Flash luu danh sach the. Log diem danh dung 0x0800F000 nen 2 page
 * 1KB ngay truoc do (0x0800E000, 0x0800E400) danh cho database the. */
#define CARDDB_START_ADDR   0x0800E000U
#define CARDDB_NUM_PAGES    2U

#define UID_LEN             4U     /* Mifare Classic 1K: UID 4 byte */

typedef enum {
    CARD_OK = 0,
    CARD_EXIST,
    CARD_NOT_FOUND,
    CARD_DB_FULL,
    CARD_FLASH_ERROR,
    CARD_BAD_ARG
} CardResult;

void       CardDB_Init(void);
CardResult CardDB_Add(const uint8_t *uid);
CardResult CardDB_Delete(const uint8_t *uid);
CardResult CardDB_Find(const uint8_t *uid, uint16_t *index_out);
bool       CardDB_IsRegistered(const uint8_t *uid);
uint16_t   CardDB_Count(void);
void       CardDB_List(void);      /* dump danh sach ra UART */
void       CardDB_EraseAll(void);

#endif /* CARD_DB_H */
