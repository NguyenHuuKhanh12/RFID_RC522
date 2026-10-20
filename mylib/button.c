#include "button.h"
#include "main.h"
#include <stdbool.h>

#define BTN_DEBOUNCE_MS  30U

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    uint8_t       mask;
    uint8_t       last_level;  /* 1 = nha, 0 = nhan */
    uint32_t      last_tick;
} Button_t;

static Button_t buttons[] = {
    { BTN_ADD_GPIO_Port,  BTN_ADD_Pin,  BUTTON_ADD,  1, 0 },
    { BTN_DEL_GPIO_Port,  BTN_DEL_Pin,  BUTTON_DEL,  1, 0 },
    { BTN_FIND_GPIO_Port, BTN_FIND_Pin, BUTTON_FIND, 1, 0 },
};

static bool Button_Falling(Button_t *b)
{
    uint32_t now = HAL_GetTick();
    uint8_t level = (HAL_GPIO_ReadPin(b->port, b->pin) == GPIO_PIN_RESET) ? 0 : 1;

    if (level == b->last_level) return false;
    if ((uint32_t)(now - b->last_tick) < BTN_DEBOUNCE_MS) return false;

    b->last_tick  = now;
    b->last_level = level;
    return (level == 0);
}

uint8_t Button_Scan(void)
{
    uint8_t pressed = 0;
    for (uint8_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        if (Button_Falling(&buttons[i])) pressed |= buttons[i].mask;
    }
    return pressed;
}
