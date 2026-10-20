/* ====================================================================
 * SV2_user.c  -  NHIEM VU SINH VIEN 2
 * "Quan ly du lieu: danh sach the trong Flash (them/xoa/tim), che do admin"
 *
 * Gom (tu user.c goc):
 *   - Danh sach the trong Flash: Card_At/Write/MarkDeleted, FindCard,
 *     AddCard, DeleteCard, GetCardCount, ListCards, EraseAllCards, USER_Init
 *   - Che do Admin (NORMAL / ADMIN_IDLE / ADD / DEL / FIND), the Admin, timeout
 *   - Quet 3 nut nhan (debounce), USER_Task
 *   - USER_HandleCard: dinh tuyen the theo che do
 *
 * Phan cua SV khac duoc tach khoi user.c:
 *   - Coi + LED xanh/do (Alert_*)                 -> SV4_oled_alert.c
 *   - Ve man hinh OLED (Screen_*, USER_ShowMessage)-> SV4_oled_alert.c
 *
 * File thay the cho: user.c
 * Header di kem: SV2_user.h (user.h, bo USER_ShowMessage)
 *
 * KY HIEU DANH DAU (grep trong file de tim):
 *   ##CROSS##  : cho nay GOI / DUNG ham, bien cua SV khac (ghi ro SV nao)
 *   ##NEW##    : ham / dong them moi de noi cac SV voi nhau, KHONG co trong code goc
 *   ##MOVED##  : code goc nam o file khac, da chuyen ve file cua SV nay
 *   Moi cau lenh con lai la code goc, giu nguyen.
 * ==================================================================== */

#include "SV2_user.h"
#include "SV3_attendance_log.h"   /* ##CROSS## SV3: ATTLOG_AddRecord(), Record_t */
#include "SV4_oled_alert.h"       /* ##CROSS## SV4: Screen_*, Alert_*, OLED_COLS */
#include <string.h>
#include <stdio.h>
#include "main.h"

#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE 1024U
#endif

/* Mot ban ghi the = 8 byte (chia het cho 2 de ghi halfword) */
#define CARD_MAGIC        0xC5A5U   /* the dang hoat dong */
#define CARD_DELETED      0x0000U   /* the da bi xoa (ghi de magic ve 0) */
#define CARD_EMPTY        0xFFFFU   /* o trong (Flash sau khi erase)     */

typedef struct __attribute__((packed)) {
    uint16_t magic;           /* CARD_MAGIC / CARD_DELETED / CARD_EMPTY */
    uint8_t  uid[UID_LEN];    /* 4 byte UID                             */
    uint16_t reserved;        /* du phong, cho du 8 byte                */
} CardRecord_t;

#define CARDS_PER_PAGE   (FLASH_PAGE_SIZE / sizeof(CardRecord_t))
#define CARD_CAPACITY    (CARDS_PER_PAGE * CARDDB_NUM_PAGES)

/* --- UID the Admin: SUA O DAY thanh the thuc te cua ban --- */
uint8_t ADMIN_UID[UID_LEN] = ADMIN_UID_DEFAULT;

static SystemMode current_mode = MODE_NORMAL;
static uint16_t   write_index  = 0;   /* o trong dau tien */
static uint32_t   admin_tick   = 0;   /* moc thoi gian de timeout Admin */

extern UART_HandleTypeDef huart1;


static void UID_ToStr(const uint8_t *uid, char *out, size_t n)
{
    (void)snprintf(out, n, "UID: %02X%02X%02X%02X",
                   uid[0], uid[1], uid[2], uid[3]);
}

/* Flash co the bi thay doi boi phep ghi -> doc qua con tro volatile */
static volatile CardRecord_t *Card_At(uint16_t index)
{
    uint32_t page   = index / CARDS_PER_PAGE;
    uint32_t offset = (index % CARDS_PER_PAGE) * sizeof(CardRecord_t);
    return (volatile CardRecord_t *)(CARDDB_START_ADDR
                                     + page * FLASH_PAGE_SIZE + offset);
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

    /* Verify: doc lai tung halfword tu Flash */
    const volatile uint16_t *f = (const volatile uint16_t *)address;
    for (uint32_t i = 0; i < count; i++) {
        if (f[i] != p[i]) return -1;
    }
    return 0;
}

/* Xoa logic: ghi de magic tu CARD_MAGIC (0xC5A5) ve CARD_DELETED (0x0000).
 * Flash NOR chi cho chuyen bit 1 -> 0 nen phep ghi nay luon hop le. */
static int Card_MarkDeleted(volatile CardRecord_t *rec)
{
    HAL_FLASH_Unlock();
    HAL_StatusTypeDef st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                                             (uint32_t)&rec->magic,
                                             CARD_DELETED);
    HAL_FLASH_Lock();
    return (st == HAL_OK && rec->magic == CARD_DELETED) ? 0 : -1;
}

bool USER_CompareUID(const uint8_t *a, const uint8_t *b)
{
    if (!a || !b) return false;
    return memcmp(a, b, UID_LEN) == 0;
}

/* ====================================================================
 * 3 CHUC NANG QUAN LY THE
 * ==================================================================== */

/* --- Chuc nang 3: TIM the --- */
CardResult USER_FindCard(const uint8_t *uid, uint16_t *index_out)
{
    if (!uid) return CARD_BAD_ARG;

    for (uint16_t i = 0; i < CARD_CAPACITY; i++) {
        volatile CardRecord_t *rec = Card_At(i);
        if (rec->magic == CARD_EMPTY) break;          /* het du lieu */
        if (rec->magic != CARD_MAGIC) continue;       /* o da xoa    */
        uint8_t stored[UID_LEN];
        for (uint8_t k = 0; k < UID_LEN; k++) stored[k] = rec->uid[k];
        if (USER_CompareUID(stored, uid)) {
            if (index_out) *index_out = i;
            return CARD_OK;
        }
    }
    return CARD_NOT_FOUND;
}

bool USER_IsCardRegistered(const uint8_t *uid)
{
    return USER_FindCard(uid, NULL) == CARD_OK;
}

/* --- Chuc nang 1: THEM the --- */
CardResult USER_AddCard(const uint8_t *uid)
{
    if (!uid) return CARD_BAD_ARG;

    /* Khong cho trung the, va khong cho them chinh the Admin */
    if (USER_CompareUID(uid, ADMIN_UID)) return CARD_EXIST;
    if (USER_FindCard(uid, NULL) == CARD_OK) return CARD_EXIST;

    if (write_index >= CARD_CAPACITY) return CARD_DB_FULL;

    CardRecord_t rec;
    rec.magic    = CARD_MAGIC;
    memcpy(rec.uid, uid, UID_LEN);
    rec.reserved = 0;

    uint32_t addr = (uint32_t)Card_At(write_index);
    if (Card_Write(addr, &rec) != 0) return CARD_FLASH_ERROR;

    write_index++;
    return CARD_OK;
}

/* --- Chuc nang 2: XOA the --- */
CardResult USER_DeleteCard(const uint8_t *uid)
{
    if (!uid) return CARD_BAD_ARG;

    uint16_t idx = 0;
    if (USER_FindCard(uid, &idx) != CARD_OK) return CARD_NOT_FOUND;

    /* ##LUU Y## Card_MarkDeleted() tra 0 khi THANH CONG, nhung dong duoi tra loi khi == 0
     * -> xoa duoc nhung van bao CARD_FLASH_ERROR. Code goc, chua sua. */
    if (Card_MarkDeleted(Card_At(idx)) == 0) return CARD_FLASH_ERROR;
    return CARD_OK;
}

uint16_t USER_GetCardCount(void)
{
    uint16_t count = 0;
    for (uint16_t i = 0; i < CARD_CAPACITY; i++) {
        volatile CardRecord_t *rec = Card_At(i);
        if (rec->magic == CARD_EMPTY) break;
        if (rec->magic == CARD_MAGIC) count++;
    }
    return count;
}

static void UART_Say(const char *s)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)strlen(s), 100);
}

/* Chi goi khi can dump danh sach the (khong dung trong luong diem danh) */
void USER_ListCards(void)
{
    char line[48];
    UART_Say("STT,UID\r\n");

    uint16_t stt = 0;
    for (uint16_t i = 0; i < CARD_CAPACITY; i++) {
        volatile CardRecord_t *rec = Card_At(i);
        if (rec->magic == CARD_EMPTY) break;
        if (rec->magic != CARD_MAGIC) continue;

        stt++;
        (void)snprintf(line, sizeof(line), "%u,%02X%02X%02X%02X\r\n",
                       (unsigned)stt, rec->uid[0], rec->uid[1],
                       rec->uid[2], rec->uid[3]);
        UART_Say(line);
    }
    UART_Say("END\r\n");
}

void USER_EraseAllCards(void)
{
    for (uint32_t p = 0; p < CARDDB_NUM_PAGES; p++) {
        Card_ErasePage(CARDDB_START_ADDR + p * FLASH_PAGE_SIZE);
    }
    write_index = 0;
}

/* ====================================================================
 * KHOI TAO
 * ==================================================================== */
void USER_Init(void)
{
    /* Quet tim o trong dau tien -> vi tri ghi tiep theo */
    write_index = CARD_CAPACITY;
    for (uint16_t i = 0; i < CARD_CAPACITY; i++) {
        if (Card_At(i)->magic == CARD_EMPTY) {
            write_index = i;
            break;
        }
    }

    current_mode = MODE_NORMAL;
    Screen_Home();  /* ##CROSS## SV4 (OLED) */
}

SystemMode USER_GetMode(void) { return current_mode; }

static void Enter_Normal(const char *why)
{
    current_mode = MODE_NORMAL;
    if (why) Screen_Result("DIEM DANH", "THOAT", why, "");  /* ##CROSS## SV4 (OLED) */
    else     Screen_Home();  /* ##CROSS## SV4 (OLED) */
}

static void Enter_AdminIdle(void)
{
    current_mode = MODE_ADMIN_IDLE;
    admin_tick   = HAL_GetTick();
    Screen_Home();  /* ##CROSS## SV4 (OLED) */
}

/* Sau 1 thao tac Admin: hien ket qua roi ve menu Admin */
static void Admin_Done(void)
{
    admin_tick   = HAL_GetTick();
    current_mode = MODE_ADMIN_IDLE;
}

/* ====================================================================
 * XU LY SU KIEN QUET THE
 * ==================================================================== */
bool USER_HandleCard(const uint8_t *uid, uint8_t uid_size)
{
    if (!uid || uid_size < UID_LEN) {
        Screen_Result("LOI", "KHONG HOP", "UID sai dinh dang", "");  /* ##CROSS## SV4 (OLED) */
        return false;
    }

    char uid_str[OLED_COLS + 1];
    UID_ToStr(uid, uid_str, sizeof(uid_str));

    /* 1. Uu tien the Admin: vao / thoat che do Admin */
    if (USER_CompareUID(uid, ADMIN_UID)) {
        if (current_mode == MODE_NORMAL) {
            Enter_AdminIdle();
        } else {
            Enter_Normal("Da thoat Admin");
        }
        return true;
    }

    /* 2. The thuong: xu ly theo che do hien tai */
    switch (current_mode) {

    case MODE_NORMAL: {
        /* Chi diem danh khi the da duoc luu vao Flash truoc do */
        if (!USER_IsCardRegistered(uid)) {
            Screen_Result("THE KHONG HOP LE", "TU CHOI", uid_str, "Chua dang ky");  /* ##CROSS## SV4 (OLED) */
					
						Alert_Start(LED_RED_Pin, 2, 250, 100); // THÊM  /* ##CROSS## SV4 (LED/coi) */
					
            return false;
        }
        /* Hop le -> ghi log Flash + xuat dong CSV ra UART (chi CSV) */
        Record_t rec;
        if (ATTLOG_AddRecord((uint8_t *)uid, &rec) != 0) {  /* ##CROSS## SV3 (nhat ky Flash) */
            Screen_Result("DIEM DANH", "LOI FLASH", uid_str, "Khong ghi duoc log");  /* ##CROSS## SV4 (OLED) */
            return false;
        }
				
				/* THEM: dung gio -> xanh + 1 bip dai, muon -> do + 3 bip ngan */
        if (rec.status == 0) Alert_Start(LED_GREEN_Pin, 1, 400, 0);  /* ##CROSS## SV4 (LED/coi) */
        else                 Alert_Start(LED_RED_Pin,   3, 120, 120);  /* ##CROSS## SV4 (LED/coi) */
				
				
        char t[OLED_COLS + 1];
        (void)snprintf(t, sizeof(t), "%02u:%02u:%02u %02u/%02u/%02u",
                       rec.hour, rec.minute, rec.second,
                       rec.day, rec.month, rec.year);
        Screen_Result("DIEM DANH OK",  /* ##CROSS## SV4 (OLED) */
                      (rec.status == 0) ? "DUNG GIO" : "MUON",
                      uid_str, t);
        return true;
    }

    case MODE_ADMIN_ADD: {
        CardResult r = USER_AddCard(uid);
        switch (r) {
        case CARD_OK:      Screen_Result("THEM THE", "THANH CONG", uid_str, "Da luu vao Flash"); break;  /* ##CROSS## SV4 (OLED) */
        case CARD_EXIST:   Screen_Result("THEM THE", "THAT BAI", uid_str, "The da ton tai");     break;  /* ##CROSS## SV4 (OLED) */
        case CARD_DB_FULL: Screen_Result("THEM THE", "THAT BAI", uid_str, "Bo nho day");         break;  /* ##CROSS## SV4 (OLED) */
        default:           Screen_Result("THEM THE", "THAT BAI", uid_str, "Loi ghi Flash");      break;  /* ##CROSS## SV4 (OLED) */
        }
        Admin_Done();
        return (r == CARD_OK);
    }

    case MODE_ADMIN_DEL: {
        CardResult r = USER_DeleteCard(uid);
        switch (r) {
        case CARD_OK:        Screen_Result("XOA THE", "THANH CONG", uid_str, "Da xoa khoi Flash"); break;  /* ##CROSS## SV4 (OLED) */
        case CARD_NOT_FOUND: Screen_Result("XOA THE", "THAT BAI", uid_str, "Khong co the nay");   break;  /* ##CROSS## SV4 (OLED) */
        default:             Screen_Result("XOA THE", "THAT BAI", uid_str, "Loi ghi Flash");      break;  /* ##CROSS## SV4 (OLED) */
        }
        Admin_Done();
        return (r == CARD_OK);
    }

    case MODE_ADMIN_FIND: {
        uint16_t idx = 0;
        CardResult r = USER_FindCard(uid, &idx);
        if (r == CARD_OK) {
            char buf[OLED_COLS + 1];
            (void)snprintf(buf, sizeof(buf), "Vi tri #%u", (unsigned)idx);
            Screen_Result("TIM THE", "CO THE", uid_str, buf);  /* ##CROSS## SV4 (OLED) */
        } else {
            Screen_Result("TIM THE", "KHONG CO", uid_str, "Chua dang ky");  /* ##CROSS## SV4 (OLED) */
        }
        Admin_Done();
        return (r == CARD_OK);
    }

    case MODE_ADMIN_IDLE:
    default:
        Screen_Result("CHE DO ADMIN", "CHON NUT", "Bam nut truoc", "roi quet the");  /* ##CROSS## SV4 (OLED) */
        admin_tick = HAL_GetTick();
        return false;
    }
}

/* ====================================================================
 * QUET NUT NHAN (non-blocking, debounce bang tick)
 * 3 nut CHI hoat dong khi dang o MODE_ADMIN_IDLE.
 * ==================================================================== */

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    uint8_t       last_level;  /* 1 = nha, 0 = nhan */
    uint32_t      last_tick;
} Button_t;

static Button_t buttons[3] = {
    { BTN_ADD_PORT,  BTN_ADD_PIN,  1, 0 },
    { BTN_DEL_PORT,  BTN_DEL_PIN,  1, 0 },
    { BTN_FIND_PORT, BTN_FIND_PIN, 1, 0 },
};

/* Tra ve true dung 1 lan tai suon xuong (nha -> nhan) */
static bool Button_Pressed(Button_t *b)
{
    uint32_t now = HAL_GetTick();
    uint8_t level = (HAL_GPIO_ReadPin(b->port, b->pin) == GPIO_PIN_RESET) ? 0 : 1;

    if (level == b->last_level) return false;
    if ((uint32_t)(now - b->last_tick) < BTN_DEBOUNCE_MS) return false;

    b->last_tick  = now;
    b->last_level = level;
    return (level == 0);
}

void USER_Task(void)
{
		Alert_Task();        //THÊM  /* ##CROSS## SV4 (LED/coi) */
	
    /* Luon doc nut de cap nhat trang thai debounce, nhung chi hanh dong
     * khi dang o che do Admin -> ngoai che do Admin nut bi vo hieu hoa. */
    bool p_add  = Button_Pressed(&buttons[0]);
    bool p_del  = Button_Pressed(&buttons[1]);
    bool p_find = Button_Pressed(&buttons[2]);

    /* Het thoi gian hien ket qua -> ve man hinh cua che do hien tai */
    Screen_ResultTask();   /* ##CROSS## SV4 (##NEW##): kiem tra het thoi gian hien ket qua; ##MOVED## tu doan if (result_showing ...) goc */

    if (current_mode == MODE_NORMAL) return;   /* nut vo hieu hoa */

    if (current_mode == MODE_ADMIN_IDLE) {
        if (p_add) {
            current_mode = MODE_ADMIN_ADD;
            admin_tick = HAL_GetTick();
            Screen_Home();
        } else if (p_del) {
            current_mode = MODE_ADMIN_DEL;
            admin_tick = HAL_GetTick();
            Screen_Home();
        } else if (p_find) {
            current_mode = MODE_ADMIN_FIND;
            admin_tick = HAL_GetTick();
            Screen_Home();
        }
    } else {
        /* Dang cho quet the: bam lai nut nao cung huy ve menu Admin */
        if (p_add || p_del || p_find) {
            Enter_AdminIdle();
        }
    }

    /* Tu dong thoat Admin neu khong thao tac gi qua lau */
    if ((uint32_t)(HAL_GetTick() - admin_tick) >= ADMIN_TIMEOUT_MS) {
        Enter_Normal("Admin het thoi gian");
    }
}
