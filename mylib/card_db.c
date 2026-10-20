#include "card_db.h"
#include <string.h>
#include <stdio.h>

#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE 1024U
#endif

/* Mot ban ghi the = 8 byte (chia het cho 2 de ghi halfword) */
#define CARD_MAGIC        0xC5A5U
#define CARD_DELETED      0x0000U
#define CARD_EMPTY        0xFFFFU
#define CARD_ALIVE   0xFFFFU   /* reserved con trong = the con hieu luc */
#define CARD_DEAD    0x0000U   /* reserved da ghi 0 = da xoa */



typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t  uid[UID_LEN];
    uint16_t reserved;
} CardRecord_t;

#define CARDS_PER_PAGE   (FLASH_PAGE_SIZE / sizeof(CardRecord_t))
#define CARD_CAPACITY    (CARDS_PER_PAGE * CARDDB_NUM_PAGES)

extern UART_HandleTypeDef huart1;

static uint16_t write_index = 0;

static volatile CardRecord_t *Card_At(uint16_t index)
{
    uint32_t page   = index / CARDS_PER_PAGE;
    uint32_t offset = (index % CARDS_PER_PAGE) * sizeof(CardRecord_t);
    return (volatile CardRecord_t *)(CARDDB_START_ADDR
                                     + page * FLASH_PAGE_SIZE + offset);
}

static inline bool Card_IsValid(volatile CardRecord_t *r)
{
    return (r->magic == CARD_MAGIC) && (r->reserved == CARD_ALIVE);
}

static void Card_ErasePage(uint32_t page_address)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0;

    erase.TypeErase   = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = page_address;
    erase.NbPages     = 1;

    HAL_FLASH_Unlock();
    (void)HAL_FLASHEx_Erase(&erase, &page_error);
    HAL_FLASH_Lock();
}

/* Ghi 1 ban ghi the vao dia chi cho truoc (o phai dang trong 0xFFFF) */
static int Card_Write(uint32_t address, const CardRecord_t *rec)
{
    const uint16_t *p = (const uint16_t *)rec;
    uint32_t count = sizeof(CardRecord_t) / 2U;

    HAL_FLASH_Unlock();
    for (uint32_t i = 0; i < count; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                              address + i * 2U, p[i]) != HAL_OK) {
            HAL_FLASH_Lock();
            return -1;
        }
    }
    HAL_FLASH_Lock();

    const volatile uint16_t *f = (const volatile uint16_t *)address;
    for (uint32_t i = 0; i < count; i++) {
        if (f[i] != p[i]) return -1;
    }
    return 0;
}

/* Xoa logic: ghi de magic CARD_MAGIC -> CARD_DELETED (Flash NOR chi 1 -> 0). */
static int Card_MarkDeleted(volatile CardRecord_t *rec)
{
    uint32_t addr = (uint32_t)&rec->reserved;

    if (rec->reserved == CARD_DEAD) return 0;

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPERR);
    (void)HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr, CARD_DEAD);
    HAL_FLASH_Lock();

    return (rec->reserved == CARD_DEAD) ? 0 : -1;
}

static void UART_Say(const char *s)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)strlen(s), 100);
}


static bool UID_Equal(const uint8_t *a, const uint8_t *b)
{
    return memcmp(a, b, UID_LEN) == 0;
}



CardResult CardDB_Find(const uint8_t *uid, uint16_t *index_out)
{
    if (!uid) return CARD_BAD_ARG;

    for (uint16_t i = 0; i < CARD_CAPACITY; i++) {
        volatile CardRecord_t *rec = Card_At(i);
        if (rec->magic == CARD_EMPTY) break;
        if (!Card_IsValid(rec)) continue;
        uint8_t stored[UID_LEN];
        for (uint8_t k = 0; k < UID_LEN; k++) stored[k] = rec->uid[k];
        if (UID_Equal(stored, uid)) {
            if (index_out) *index_out = i;
            return CARD_OK;
        }
    }
    return CARD_NOT_FOUND;
}

bool CardDB_IsRegistered(const uint8_t *uid)
{
    return CardDB_Find(uid, NULL) == CARD_OK;
}

CardResult CardDB_Add(const uint8_t *uid)
{
    if (!uid) return CARD_BAD_ARG;
    if (CardDB_Find(uid, NULL) == CARD_OK) return CARD_EXIST;
    if (write_index >= CARD_CAPACITY) return CARD_DB_FULL;

    CardRecord_t rec;
    rec.magic    = CARD_MAGIC;
    memcpy(rec.uid, uid, UID_LEN);
    rec.reserved = CARD_ALIVE;

    uint32_t addr = (uint32_t)Card_At(write_index);
    if (Card_Write(addr, &rec) != 0) return CARD_FLASH_ERROR;

    write_index++;
    return CARD_OK;
}

CardResult CardDB_Delete(const uint8_t *uid)
{
    if (!uid) return CARD_BAD_ARG;

    uint16_t idx = 0;
    if (CardDB_Find(uid, &idx) != CARD_OK) return CARD_NOT_FOUND;

    if (Card_MarkDeleted(Card_At(idx)) != 0) return CARD_FLASH_ERROR;
    return CARD_OK;
}

uint16_t CardDB_Count(void)
{
    uint16_t count = 0;
    for (uint16_t i = 0; i < CARD_CAPACITY; i++) {
        volatile CardRecord_t *rec = Card_At(i);
        if (rec->magic == CARD_EMPTY) break;
        if (Card_IsValid(rec)) count++;
    }
    return count;
}

void CardDB_List(void)
{
    char line[48];
    UART_Say("STT,UID\r\n");

    uint16_t stt = 0;
    for (uint16_t i = 0; i < CARD_CAPACITY; i++) {
        volatile CardRecord_t *rec = Card_At(i);
        if (rec->magic == CARD_EMPTY) break;
        if (!Card_IsValid(rec)) continue;

        stt++;
        (void)snprintf(line, sizeof(line), "%u,%02X%02X%02X%02X\r\n",
                       (unsigned)stt, rec->uid[0], rec->uid[1],
                       rec->uid[2], rec->uid[3]);
        UART_Say(line);
    }
    UART_Say("END\r\n");
}

void CardDB_EraseAll(void)
{
    for (uint32_t p = 0; p < CARDDB_NUM_PAGES; p++) {
        Card_ErasePage(CARDDB_START_ADDR + p * FLASH_PAGE_SIZE);
    }
    write_index = 0;
}

void CardDB_Init(void)
{
    write_index = CARD_CAPACITY;
    for (uint16_t i = 0; i < CARD_CAPACITY; i++) {
        if (Card_At(i)->magic == CARD_EMPTY) {
            write_index = i;
            break;
        }
    }
}
