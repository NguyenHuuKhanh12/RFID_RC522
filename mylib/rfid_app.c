#include "rfid_app.h"
#include <stdio.h>
#include <string.h>
#include "attendance_log.h"
#include "user.h"
#include "alert.h"
#include "ui.h"
#include "OLED.h"

RC522_Handle rc522 = {0};
volatile uint8_t rc522_version = 0;
volatile RC522_Status rfid_init_status = RC522_BAD_ARG;
volatile RC522_Status rfid_last_status = RC522_NO_CARD;
volatile RC522_UID rfid_last_uid = {{0}, 0, 0};
volatile uint32_t rfid_event_count = 0;
volatile RC522_SPITest rfid_spi_diag = {0};
volatile RC522_Status rfid_spi_diag_status = RC522_BAD_ARG;
volatile RC522_Status rfid_error_status = RC522_OK;
volatile RC522_DebugStage rfid_error_stage = RC522_DBG_NONE;
volatile uint8_t rfid_error_command = 0;
volatile uint8_t rfid_error_irq = 0;
volatile uint8_t rfid_error_reg = 0;
volatile uint8_t rfid_error_len = 0;
volatile uint8_t rfid_error_bits = 0;
volatile uint8_t rfid_error_registers_valid = 0;
uint8_t event = 1;

static UART_HandleTypeDef *rfid_uart;

static void SaveError(RC522_Status status)
{
    rfid_error_status = status;
    rfid_error_stage = rc522_debug_stage;
    rfid_error_command = rc522_debug_command;
    rfid_error_irq = rc522_debug_irq;
    rfid_error_reg = rc522_debug_error_reg;
    rfid_error_len = rc522_debug_rx_len;
    rfid_error_bits = rc522_debug_last_bits;
    rfid_error_registers_valid = rc522_debug_registers_valid;
}

static void RFID_Task(void)
{
    static uint32_t last_poll_ms;
    static uint32_t last_report_ms;
    static uint8_t reported_error;
    RC522_UID uid;
    uint32_t now = HAL_GetTick();

    if ((uint32_t)(now - last_poll_ms) < 30U) return;
    last_poll_ms = now;

    RC522_Status st = RC522_Poll(&rc522, &uid);
    rfid_last_status = st;

    if (st == RC522_OK)
    {
        rfid_last_uid = uid;
        rfid_event_count++;
        reported_error = 0;

        bool accepted = USER_HandleCard(uid.bytes, uid.size);

        Alert_StatusBlink(accepted ? 1 : 3, accepted ? 2000 : 150);
    }
    else if (st != RC522_NO_CARD && st != RC522_DUPLICATE)
    {
        SaveError(st);
        now = HAL_GetTick();
        if (!reported_error || (uint32_t)(now - last_report_ms) >= 1500U)
        {
            char l2[OLED_COLS + 1];
            (void)snprintf(l2, sizeof(l2), "%s", RC522_StatusString(st));
            if (st == RC522_COLLISION)
                UI_ShowResult("LOI DOC THE", "NHIEU THE", "Chi dua 1 the", l2);
            else
                UI_ShowResult("LOI DOC THE", "THU LAI", "Dua the lai gan", l2);
            Alert_StatusBlink(st == RC522_COLLISION ? 2 : 3, 150);

            reported_error = 1;
            last_report_ms = now;
        }
    }
}

void RFID_App_Init(SPI_HandleTypeDef *spi, UART_HandleTypeDef *uart)
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    rfid_uart = uart;

    rc522.spi = spi;
    rc522.cs_port = RC522_CS_GPIO_Port;
    rc522.cs_pin = RC522_CS_Pin;
    rc522.rst_port = RC522_RST_GPIO_Port;
    rc522.rst_pin = RC522_RST_Pin;
    rc522.spi_timeout_ms = 20;
    rc522.command_timeout_ms = 50;
    rc522.removal_ms = 300;

    rfid_init_status = RC522_Init(&rc522);
    uint8_t version = 0;
    RC522_Status version_status = RC522_GetVersion(&rc522, &version);
    rc522_version = version;

    char text[OLED_COLS + 1];

    if (rfid_init_status != RC522_OK || version_status != RC522_OK)
    {
        rfid_error_status = (rfid_init_status != RC522_OK)
            ? rfid_init_status : version_status;
        (void)snprintf(text, sizeof(text), "VER=%02X %s", (unsigned)version,
                       RC522_StatusString(rfid_error_status));
        OLED_ShowFatal("LOI RC522", "Kiem tra day noi", text);
        while (1)
        {
            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
            HAL_Delay(150);
        }
    }

    RC522_SPITest spi_result;
    RC522_Status spi_status = RC522_RunSPIDiagnostic(&rc522, &spi_result);
    rfid_spi_diag = spi_result;
    rfid_spi_diag_status = spi_status;

    /* Do not accept card data while the SPI/FIFO integrity check fails. */
    if (spi_status != RC522_OK)
    {
        rfid_error_status = spi_status;
        (void)snprintf(text, sizeof(text), "FIFO=%u IO=%u",
                       (unsigned)spi_result.fifo_mismatches,
                       (unsigned)spi_result.io_errors);
        OLED_ShowFatal("LOI SPI RC522", "SPI khong on dinh", text);
        while (1)
        {
            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
            HAL_Delay(150);
        }
    }

    for (uint8_t i = 0; i < 2; i++)
    {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
        HAL_Delay(120);
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        HAL_Delay(120);
    }
}

void RFID_App_Task(void)
{
    Alert_Task();
    USER_Task();
    RFID_Task();
}
