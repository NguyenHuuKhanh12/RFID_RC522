/* ====================================================================
 * SV2_user.h  -  NHIEM VU SINH VIEN 2  (header di kem SV2_user.c)
 * Tu: user.h (nguyen ban), tru USER_ShowMessage() da chuyen sang SV4.
 * Phu thuoc SV khac: khong co (chi dung main.h / HAL).
 * ==================================================================== */
#ifndef SV2_USER_H
#define SV2_USER_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ====================================================================
 * 1. CAU HINH
 * ==================================================================== */

/* Vung Flash luu danh sach the da dang ky.
 * Log diem danh dang dung 0x0800F000 (4 page = 4KB), nen database the
 * nam o 2 page ngay truoc do: 0x0800E000 va 0x0800E400 (page size 1KB). */
#define CARDDB_START_ADDR   0x0800E000U
#define CARDDB_NUM_PAGES    2U

#define UID_LEN             4U     /* Mifare Classic 1K: UID 4 byte */

/* UID the Admin - MAC DINH, sinh vien doi lai thanh the thuc te cua minh.
 * Sua gia tri trong mang ADMIN_UID[] o file user.c */
#define ADMIN_UID_DEFAULT   { 0x67, 0x06, 0x0C, 0x07 }

/* Nut nhan (tat ca pull-up: nhan = GPIO_PIN_RESET) */
#define BTN_ADD_PORT        BTN_ADD_GPIO_Port
#define BTN_ADD_PIN         BTN_ADD_Pin
#define BTN_DEL_PORT        BTN_DEL_GPIO_Port
#define BTN_DEL_PIN         BTN_DEL_Pin
#define BTN_FIND_PORT       BTN_FIND_GPIO_Port
#define BTN_FIND_PIN        BTN_FIND_Pin

#define BTN_DEBOUNCE_MS     30U
#define ADMIN_TIMEOUT_MS    30000U  /* 30s khong thao tac -> tu thoat Admin */

/* ====================================================================
 * 2. TRANG THAI HE THONG (STATE MACHINE)
 * ==================================================================== */
typedef enum {
    MODE_NORMAL = 0,  /* Diem danh binh thuong                       */
    MODE_ADMIN_IDLE,  /* Da quet the admin, cho bam nut              */
    MODE_ADMIN_ADD,   /* Bam nut 1: cho quet the de THEM             */
    MODE_ADMIN_DEL,   /* Bam nut 2: cho quet the de XOA              */
    MODE_ADMIN_FIND   /* Bam nut 3: cho quet the de TIM              */
} SystemMode;

/* Ket qua tra ve cua cac ham quan ly the */
typedef enum {
    CARD_OK = 0,        /* Thao tac thanh cong          */
    CARD_EXIST,         /* The da ton tai trong Flash   */
    CARD_NOT_FOUND,     /* Khong tim thay the           */
    CARD_DB_FULL,       /* Het cho luu                  */
    CARD_FLASH_ERROR,   /* Loi ghi/xoa Flash            */
    CARD_BAD_ARG        /* Tham so khong hop le         */
} CardResult;

/* ====================================================================
 * 3. API
 * ==================================================================== */

/* Khoi tao: quet Flash tim vi tri ghi tiep theo. Goi 1 lan trong main(). */
void USER_Init(void);

/* Quet nut nhan + xu ly timeout. Goi lien tuc trong while(1). */
void USER_Task(void);

/* Goi khi RC522 doc duoc 1 the moi. Tu dinh tuyen theo che do hien tai:
 *  - MODE_NORMAL     : kiem tra the co trong Flash khong -> diem danh/bao loi
 *  - MODE_ADMIN_*    : them / xoa / tim the
 * Tra ve true neu the duoc chap nhan (diem danh thanh cong hoac thao tac OK). */
bool USER_HandleCard(const uint8_t *uid, uint8_t uid_size);

/* Che do hien tai (chi doc tu ben ngoai) */
SystemMode USER_GetMode(void);

/* 3 chuc nang quan ly the trong Flash */
CardResult USER_AddCard(const uint8_t *uid);      /* Chuc nang 1: THEM */
CardResult USER_DeleteCard(const uint8_t *uid);   /* Chuc nang 2: XOA  */
CardResult USER_FindCard(const uint8_t *uid, uint16_t *index_out); /* Chuc nang 3: TIM */

/* Kiem tra nhanh: the da duoc dang ky trong Flash chua? */
bool USER_IsCardRegistered(const uint8_t *uid);

/* Tien ich */
uint16_t USER_GetCardCount(void);
void     USER_ListCards(void);   /* Dump danh sach the ra UART */
void     USER_EraseAllCards(void);
bool     USER_CompareUID(const uint8_t *a, const uint8_t *b);

/* ##MOVED## USER_ShowMessage() (hien thong bao OLED) -> SV4_oled_alert.h (nhiem vu SV4) */

/* Mang UID admin (sua truc tiep trong user.c) */
extern uint8_t ADMIN_UID[UID_LEN];

#ifdef __cplusplus
}
#endif

#endif /* SV2_USER_H */
