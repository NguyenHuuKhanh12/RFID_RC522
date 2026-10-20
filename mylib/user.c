/* ====================================================================
 * user.c - May trang thai Admin va phan phoi su kien quet the
 * 3 nut nhan chi hoat dong sau khi quet the Admin.
 * Che do diem danh: chi the da luu trong Flash moi duoc ghi log.
 * ==================================================================== */

#include "user.h"
#include "card_db.h"
#include "alert.h"
#include "button.h"
#include "ui.h"
#include "attendance_log.h"
#include "OLED.h"
#include <stdio.h>
#include <string.h>

/* UID the Admin - sinh vien doi lai thanh the thuc te cua minh */
#define ADMIN_UID_DEFAULT   { 0x67, 0x06, 0x0C, 0x07 }
#define ADMIN_TIMEOUT_MS    30000U  /* 30s khong thao tac -> tu thoat Admin */

static const uint8_t ADMIN_UID[UID_LEN] = ADMIN_UID_DEFAULT;

static SystemMode current_mode = MODE_NORMAL;
static uint32_t   admin_tick   = 0;

static bool UID_Equal(const uint8_t *a, const uint8_t *b)
{
    return memcmp(a, b, UID_LEN) == 0;
}

static void UID_ToStr(const uint8_t *uid, char *out, size_t n)
{
    (void)snprintf(out, n, "UID: %02X%02X%02X%02X",
                   uid[0], uid[1], uid[2], uid[3]);
}

/* Man hinh "nen" tuong ung voi che do hien tai */
static void Screen_Home(void)
{
    char buf[OLED_COLS + 1];
    UI_CancelResult();

    switch (current_mode) {
    case MODE_NORMAL:
        (void)snprintf(buf, sizeof(buf), "So the: %u", (unsigned)CardDB_Count());
        UI_Draw("DIEM DANH", "QUET THE", buf, "");
        break;
    case MODE_ADMIN_ADD:
        UI_Draw("ADMIN - THEM THE", "QUET THE", "Quet the can them", "Bam nut: Huy");
        break;
    case MODE_ADMIN_DEL:
        UI_Draw("ADMIN - XOA THE", "QUET THE", "Quet the can xoa", "Bam nut: Huy");
        break;
    case MODE_ADMIN_FIND:
        UI_Draw("ADMIN - TIM THE", "QUET THE", "Quet the can tim", "Bam nut: Huy");
        break;
    case MODE_ADMIN_IDLE:
    default:
        UI_Draw("CHE DO ADMIN", "MENU", "1:Them 2:Xoa 3:Tim", "The Admin: Thoat");
        break;
    }
}

static void Enter_Normal(const char *why)
{
    current_mode = MODE_NORMAL;
    if (why) UI_ShowResult("DIEM DANH", "THOAT", why, "");
    else     Screen_Home();
}

static void Enter_AdminIdle(void)
{
    current_mode = MODE_ADMIN_IDLE;
    admin_tick   = HAL_GetTick();
    Screen_Home();
}

/* Sau 1 thao tac Admin: hien ket qua roi ve menu Admin */
static void Admin_Done(void)
{
    admin_tick   = HAL_GetTick();
    current_mode = MODE_ADMIN_IDLE;
}

void USER_Init(void)
{
    CardDB_Init();
    current_mode = MODE_NORMAL;
    Screen_Home();
}

SystemMode USER_GetMode(void)
{
    return current_mode;
}

bool USER_HandleCard(const uint8_t *uid, uint8_t uid_size)
{
    if (!uid || uid_size < UID_LEN) {
        UI_ShowResult("LOI", "KHONG HOP", "UID sai dinh dang", "");
        return false;
    }

    char uid_str[OLED_COLS + 1];
    UID_ToStr(uid, uid_str, sizeof(uid_str));

    /* 1. Uu tien the Admin: vao / thoat che do Admin */
    if (UID_Equal(uid, ADMIN_UID)) {
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
        if (!CardDB_IsRegistered(uid)) {
            UI_ShowResult("THE KHONG HOP LE", "TU CHOI", uid_str, "Chua dang ky");
            Alert_Start(LED_RED_Pin, 2, 250, 100);
            return false;
        }
        Record_t rec;
        if (ATTLOG_AddRecord((uint8_t *)uid, &rec) != 0) {
            UI_ShowResult("DIEM DANH", "LOI FLASH", uid_str, "Khong ghi duoc log");
            return false;
        }

        /* Dung gio -> xanh + 1 bip dai, muon -> do + 3 bip ngan */
        if (rec.status == 0) Alert_Start(LED_GREEN_Pin, 1, 400, 0);
        else                 Alert_Start(LED_RED_Pin,   3, 120, 120);

        char t[OLED_COLS + 1];
        (void)snprintf(t, sizeof(t), "%02u:%02u:%02u %02u/%02u/%02u",
                       rec.hour, rec.minute, rec.second,
                       rec.day, rec.month, rec.year);
        UI_ShowResult("DIEM DANH OK",
                      (rec.status == 0) ? "DUNG GIO" : "MUON",
                      uid_str, t);
        return true;
    }

    case MODE_ADMIN_ADD: {
        CardResult r = CardDB_Add(uid);
        switch (r) {
        case CARD_OK:      UI_ShowResult("THEM THE", "THANH CONG", uid_str, "Da luu vao Flash"); break;
        case CARD_EXIST:   UI_ShowResult("THEM THE", "THAT BAI", uid_str, "The da ton tai");     break;
        case CARD_DB_FULL: UI_ShowResult("THEM THE", "THAT BAI", uid_str, "Bo nho day");         break;
        default:           UI_ShowResult("THEM THE", "THAT BAI", uid_str, "Loi ghi Flash");      break;
        }
        Admin_Done();
        return (r == CARD_OK);
    }

    case MODE_ADMIN_DEL: {
        CardResult r = CardDB_Delete(uid);
        switch (r) {
        case CARD_OK:        UI_ShowResult("XOA THE", "THANH CONG", uid_str, "Da xoa khoi Flash"); break;
        case CARD_NOT_FOUND: UI_ShowResult("XOA THE", "THAT BAI", uid_str, "Khong co the nay");   break;
        default:             UI_ShowResult("XOA THE", "THAT BAI", uid_str, "Loi ghi Flash");      break;
        }
        Admin_Done();
        return (r == CARD_OK);
    }

    case MODE_ADMIN_FIND: {
        uint16_t idx = 0;
        CardResult r = CardDB_Find(uid, &idx);
        if (r == CARD_OK) {
            char buf[OLED_COLS + 1];
            (void)snprintf(buf, sizeof(buf), "Vi tri #%u", (unsigned)idx);
            UI_ShowResult("TIM THE", "CO THE", uid_str, buf);
        } else {
            UI_ShowResult("TIM THE", "KHONG CO", uid_str, "Chua dang ky");
        }
        Admin_Done();
        return (r == CARD_OK);
    }

    case MODE_ADMIN_IDLE:
    default:
        UI_ShowResult("CHE DO ADMIN", "CHON NUT", "Bam nut truoc", "roi quet the");
        admin_tick = HAL_GetTick();
        return false;
    }
}

void USER_Task(void)
{
    uint8_t pressed = Button_Scan();
    bool p_add  = (pressed & BUTTON_ADD)  != 0;
    bool p_del  = (pressed & BUTTON_DEL)  != 0;
    bool p_find = (pressed & BUTTON_FIND) != 0;

    if (UI_ResultExpired()) Screen_Home();

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

    if ((uint32_t)(HAL_GetTick() - admin_tick) >= ADMIN_TIMEOUT_MS) {
        Enter_Normal("Admin het thoi gian");
    }
}
