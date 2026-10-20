#include "ui.h"
#include "OLED.h"
#include <stddef.h>

#define RESULT_HOLD_MS  2000U

static uint8_t  result_showing = 0;
static uint32_t result_tick    = 0;

void UI_Draw(const char *title, const char *big, const char *l1, const char *l2)
{
    OLED_Clear();
    OLED_PrintCenter(0, title ? title : "");
    OLED_InvertRow(0);
    if (big) OLED_PrintLargeCenter(2, big);
    if (l1)  OLED_PrintCenter(5, l1);
    if (l2)  OLED_PrintCenter(6, l2);
    (void)OLED_Update();
}

void UI_ShowResult(const char *title, const char *big, const char *l1, const char *l2)
{
    UI_Draw(title, big, l1, l2);
    result_showing = 1;
    result_tick    = HAL_GetTick();
}

void UI_CancelResult(void)
{
    result_showing = 0;
}

bool UI_ResultExpired(void)
{
    if (result_showing && (uint32_t)(HAL_GetTick() - result_tick) >= RESULT_HOLD_MS) {
        result_showing = 0;
        return true;
    }
    return false;
}
