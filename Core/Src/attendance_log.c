/* ====================================================================
 * SV3_attendance_log.c  -  NHIEM VU SINH VIEN 3
 * "Nhat ky diem danh: timestamp RTC, bo dem vong luu Flash, xuat CSV qua UART"
 *
 * Gom (tu attendance_log.c goc): ATTLOG_Init (RTC + quet Flash), GetTime (RTC),
 * AddRecord (ghi Flash vong 4 page), ExportSingleRecord / ExportCSV (UART),
 * EraseAll, OnUartCommand ('*' xuat CSV, '#' xoa).
 *
 * Phan cua SV khac duoc tach khoi file nay:
 *   - ATTLOG_Classify (xu ly di muon theo gio quy dinh) -> SV4_oled_alert.c
 *
 * Luu y: vong lap nhan UART ('*' / '#') nam trong main.c (khong sua),
 *        goi ATTLOG_OnUartCommand() cua file nay.
 *
 * File thay the cho: attendance_log.c
 * Header di kem: SV3_attendance_log.h (attendance_log.h)
 *
 * KY HIEU DANH DAU (grep trong file de tim):
 *   ##CROSS##  : cho nay GOI / DUNG ham, bien cua SV khac (ghi ro SV nao)
 *   ##NEW##    : ham / dong them moi de noi cac SV voi nhau, KHONG co trong code goc
 *   ##MOVED##  : code goc nam o file khac, da chuyen ve file cua SV nay
 *   Moi cau lenh con lai la code goc, giu nguyen.
 * ==================================================================== */

#include "SV3_attendance_log.h"
#include "SV4_oled_alert.h"   /* ##CROSS## SV4: USER_ShowMessage(), ATTLOG_Classify() */
#include <string.h>
#include <stdio.h>

#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE 1024U  // F103: 1KB, F411: xem lại trong hal_flash.h
#endif

#define FLASH_START_ADDR  0x0800F000U
#define NUM_PAGES         4U
#define RECORD_MAGIC      0xA55AU
#define RECORD_EMPTY      0xFFFFU
#define RECORDS_PER_PAGE  (FLASH_PAGE_SIZE / sizeof(Record_t))

#define RTC_INIT_MARKER   0x4205U


static RTC_HandleTypeDef *local_hrtc = NULL;
static uint8_t  current_page = 0;
static uint16_t current_slot = 0;
static uint32_t current_stt_counter = 0; //Biến lưu số thứ tự quẹt thẻ độc lập

extern UART_HandleTypeDef huart1;

// ===========================================================================

/* ---------- Private ---------- */

static void Flash_ErasePage(uint32_t page_address)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0;

    erase.TypeErase   = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = page_address;
    erase.NbPages     = 1;

    HAL_FLASH_Unlock();
    HAL_FLASHEx_Erase(&erase, &page_error);
    HAL_FLASH_Lock();
}

static int Flash_WriteRecord(uint32_t address, Record_t *record)
{
    uint16_t *p = (uint16_t *)record;
    uint32_t count = sizeof(Record_t) / 2;

    HAL_FLASH_Unlock();
    for (uint32_t i = 0; i < count; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                              address + i * 2, p[i]) != HAL_OK) {
            HAL_FLASH_Lock();
            return -1;
        }
    }
    HAL_FLASH_Lock();

    // verify
    if (memcmp((void*)address, record, sizeof(Record_t)) != 0) {
        return -1;
    }
    return 0;
}

static void ATTLOG_GetTime(Record_t *record)
{
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    if (!local_hrtc) return;
    HAL_RTC_GetTime(local_hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(local_hrtc, &sDate, RTC_FORMAT_BIN);

    record->hour   = sTime.Hours;
    record->minute = sTime.Minutes;
    record->second = sTime.Seconds;
    record->day    = sDate.Date;
    record->month  = sDate.Month;
    record->year   = sDate.Year;
}

/* ##CROSS## ATTLOG_Classify() (xu ly di muon theo gio quy dinh) la nhiem vu SV4
 * -> da chuyen sang SV4_oled_alert.c (##MOVED##), prototype trong SV4_oled_alert.h. */

static int ATTLOG_IsWrapped(void)
{
    // Neu page ke tiep vi tri ghi hien tai van con du lieu cu -> da quay vong
    uint8_t next = (current_page + 1) % NUM_PAGES;
    Record_t *rec = (Record_t *)(FLASH_START_ADDR + next * FLASH_PAGE_SIZE);
    return (rec->magic == RECORD_MAGIC);
}

/* ---------- Public ---------- */

void ATTLOG_Init(RTC_HandleTypeDef *hrtc)
{
    local_hrtc = hrtc;
    HAL_PWR_EnableBkUpAccess();

    // Chi khoi tao gio 1 lan dau tien
    if (HAL_RTCEx_BKUPRead(local_hrtc, RTC_BKP_DR1) != RTC_INIT_MARKER) {
        RTC_TimeTypeDef sTime = {0};
        sTime.Hours = 9; sTime.Minutes = 12; sTime.Seconds = 0;
        HAL_RTC_SetTime(local_hrtc, &sTime, RTC_FORMAT_BIN);

        RTC_DateTypeDef sDate = {0};
        sDate.Date = 13; sDate.Month = 10; sDate.Year = 26;
        sDate.WeekDay = RTC_WEEKDAY_MONDAY;
        HAL_RTC_SetDate(local_hrtc, &sDate, RTC_FORMAT_BIN);

        HAL_RTCEx_BKUPWrite(local_hrtc, RTC_BKP_DR1, RTC_INIT_MARKER);
    }
		
		current_stt_counter = 0;
    // Quet tim o trong dau tien -> vi tri ghi tiep theo
    current_page = 0;
    current_slot = 0;

    for (uint8_t p = 0; p < NUM_PAGES; p++) {
        uint32_t page_addr = FLASH_START_ADDR + (uint32_t)p * FLASH_PAGE_SIZE;
        for (uint16_t s = 0; s < RECORDS_PER_PAGE; s++) {
            Record_t *rec = (Record_t *)(page_addr + (uint32_t)s * sizeof(Record_t));
					
						if (rec->magic == RECORD_MAGIC) {
                current_stt_counter++; 
            }
						
            if (rec->magic == RECORD_EMPTY) {
                current_page = p;
                current_slot = s;
                return;
            }
        }
    }

    // Flash day kin: xoa page 0, ghi de tu dau (bat dau vong moi)
    Flash_ErasePage(FLASH_START_ADDR);
    current_page = 0;
    current_slot = 0;
		
}

int ATTLOG_AddRecord(uint8_t *uid, Record_t *out)
{
    if (!uid || !local_hrtc) return -1;

    Record_t rec = {0};
    rec.magic = RECORD_MAGIC;
    memcpy(rec.uid, uid, 4);
    ATTLOG_GetTime(&rec);
    rec.status = ATTLOG_Classify(rec.hour, rec.minute);  /* ##CROSS## SV4 (gio quy dinh di muon) */

    // Ring buffer: het page thi sang page ke tiep + xoa page do truoc
    if (current_slot >= RECORDS_PER_PAGE) {
        current_page = (current_page + 1) % NUM_PAGES;
        current_slot = 0;
        Flash_ErasePage(FLASH_START_ADDR + (uint32_t)current_page * FLASH_PAGE_SIZE);
    }

    uint32_t addr = FLASH_START_ADDR
                  + (uint32_t)current_page * FLASH_PAGE_SIZE
                  + (uint32_t)current_slot * sizeof(Record_t);

    if (Flash_WriteRecord(addr, &rec) != 0) return -1;

    current_slot++;
    if (out) *out = rec;
    // Sinh vien 2: UART chi xuat 1 dong CSV sau khi ghi thanh cong
		current_stt_counter++;
    ATTLOG_ExportSingleRecord(&rec, current_stt_counter);
    return 0;
}

void ATTLOG_ExportSingleRecord(Record_t *rec, uint16_t stt)
{
    if (!rec) return;

    char line[96];
    int len = snprintf(line, sizeof(line),
        "%u,%02X%02X%02X%02X,%02u/%02u/%04u,%02u:%02u:%02u,%s\r\n",
        (unsigned)stt,
        rec->uid[0], rec->uid[1], rec->uid[2], rec->uid[3],
        rec->day, rec->month, 2000U + rec->year,
        rec->hour, rec->minute, rec->second,
        (rec->status == 0) ? "DUNG_GIO" : "MUON");

    if (len > 0) {
        HAL_UART_Transmit(&huart1, (uint8_t*)line, (uint16_t)len, 200);
    }
}

void ATTLOG_ExportCSV(void)
{
    const char *header = "STT,UID,Ngay,Gio,TrangThai\r\n";
    HAL_UART_Transmit(&huart1, (uint8_t*)header, strlen(header), 200);

    uint8_t start_page = 0;
    if (ATTLOG_IsWrapped()) {
        start_page = (current_page + 1) % NUM_PAGES;
    }

    char line[96];
    uint32_t stt = 0;

    for (uint8_t i = 0; i < NUM_PAGES; i++) {
        uint8_t p = (start_page + i) % NUM_PAGES;
        uint32_t page_addr = FLASH_START_ADDR + (uint32_t)p * FLASH_PAGE_SIZE;

        uint16_t limit = RECORDS_PER_PAGE;
        if (!ATTLOG_IsWrapped() && p == current_page) {
            limit = current_slot; // page chua ghi het thi chi doc toi slot hien tai
        }
        // Neu da wrapped: page current la page dang ghi do -> cung chi doc toi current_slot
        if (ATTLOG_IsWrapped() && p == current_page) {
            limit = current_slot;
        }

        for (uint16_t s = 0; s < limit; s++) {
            Record_t *r = (Record_t *)(page_addr + (uint32_t)s * sizeof(Record_t));
            if (r->magic != RECORD_MAGIC) continue;

            stt++;
            int len = snprintf(line, sizeof(line),
                "%lu,%02X%02X%02X%02X,%02u/%02u/%04u,%02u:%02u:%02u,%s\r\n",
                (unsigned long)stt,
                r->uid[0], r->uid[1], r->uid[2], r->uid[3],
                r->day, r->month, 2000U + r->year,
                r->hour, r->minute, r->second,
                (r->status == 0) ? "DUNG_GIO" : "MUON");

            if (len > 0) {
                HAL_UART_Transmit(&huart1, (uint8_t*)line, (uint16_t)len, 200);
                HAL_Delay(5); // tranh tran UART khi dump dai
            }
        }
    }

}

void ATTLOG_EraseAll(void)
{
    for (uint8_t p = 0; p < NUM_PAGES; p++) {
        Flash_ErasePage(FLASH_START_ADDR + (uint32_t)p * FLASH_PAGE_SIZE);
    }
    current_page = 0;
    current_slot = 0;
		current_stt_counter =0;
}

// Goi ham nay trong main khi nhan duoc 1 byte UART: '*' xuat CSV, '#' xoa
void ATTLOG_OnUartCommand(uint8_t cmd)
{
    if (cmd == '*') {
        ATTLOG_ExportCSV();
    } else if (cmd == '#') {
        ATTLOG_EraseAll();
        // Thong bao tren OLED, khong ghi vao UART de log chi con CSV
        USER_ShowMessage("LOG DIEM DANH", "DA XOA", "Xoa toan bo log", "");  /* ##CROSS## SV4 (OLED) */
    }
}
