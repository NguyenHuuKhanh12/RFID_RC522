#ifndef ATTENDANCE_LOG_H
#define ATTENDANCE_LOG_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

// 1. Khai b�o d?a ch? Flash
// Ch?n Page 60 (G?n cu?i b? nh? c?a STM32F103C8T6) d? kh�ng b? d� v�o code chuong tr�nh
#define FLASH_USER_START_ADDR   0x0800F000


// 2. �?nh nghia c?u tr�c 1 b?n ghi di?m danh (��ng 16 byte)
typedef struct __attribute__((packed)) {
    uint16_t magic;     // 2 byte: M� x�c nhann c� du lieu (V� d?: 0xA55A)
    uint8_t  uid[4];    // 4 byte: M� the RFID (Mifare 1K thu?ng c� 4 byte)
    uint8_t  status;     // 1 byte: Su kien (0 = V�O, 1 = RA)

    // Luu tr?c ti?p th?i gian d?c t? CubeMX
    uint8_t  day;       // 1 byte: Ng�y
    uint8_t  month;     // 1 byte: Th�ng
    uint8_t  year;      // 1 byte: Nam (Ch? luu 2 s? cu?i, vd: 26)
    uint8_t  hour;      // 1 byte: Gio
    uint8_t  minute;    // 1 byte: Ph�t
    uint8_t  second;    // 1 byte: Gi�y

    uint16_t crc;       // 2 byte: M� ki?m tra l?i (T?m d? 0)
    uint8_t  padding;   // 1 byte: �em th�m v�o cho t?ng d? tr�n 16 byte
} Record_t;

// 3. khai báo hàm giao tiep flash
// hàm quan lí thoi gian
void ATTLOG_Init(RTC_HandleTypeDef *hrtc);
int  ATTLOG_AddRecord(uint8_t *uid, Record_t *out); /* 0 = OK, out co the NULL */
void ATTLOG_ExportSingleRecord(Record_t *rec, uint16_t stt);
void ATTLOG_ExportCSV(void);
void ATTLOG_EraseAll(void);
void ATTLOG_OnUartCommand(uint8_t cmd);
void Signal_Update(void);


#endif // ATTENDANCE_LOG_H
