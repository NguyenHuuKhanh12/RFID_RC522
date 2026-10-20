#include "alert.h"
#include "main.h"

static uint8_t  led_active  = 0;
static uint32_t led_off_at  = 0;
static uint8_t  beeps_left  = 0;
static uint8_t  buzzer_on   = 0;
static uint16_t beep_on_ms  = 0;
static uint16_t beep_off_ms = 0;
static uint32_t buzzer_next = 0;

static uint8_t  status_edges_left;
static uint32_t status_next_ms;
static uint32_t status_half_period_ms;

void Alert_Start(uint16_t led_pin, uint8_t beeps, uint16_t on_ms, uint16_t off_ms)
{
    uint32_t now = HAL_GetTick();

    HAL_GPIO_WritePin(GPIOB, LED_GREEN_Pin | LED_RED_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, led_pin, GPIO_PIN_SET);
    led_active = 1;
    led_off_at = now + 2000;

    beeps_left  = beeps;
    beep_on_ms  = on_ms;
    beep_off_ms = off_ms;
    HAL_GPIO_WritePin(GPIOB, BUZZER_Pin, GPIO_PIN_SET);
    buzzer_on   = 1;
    buzzer_next = now + on_ms;
}

void Alert_StatusBlink(uint8_t pulses, uint32_t half_period_ms)
{
    if (!pulses) return;

    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
    status_edges_left = (uint8_t)(2U * pulses - 1U);
    status_half_period_ms = half_period_ms;
    status_next_ms = HAL_GetTick() + half_period_ms;
}

static void Status_Task(void)
{
    uint32_t now = HAL_GetTick();
    if (status_edges_left && (int32_t)(now - status_next_ms) >= 0)
    {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        status_edges_left--;
        status_next_ms = now + status_half_period_ms;
        if (!status_edges_left)
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    }
}

void Alert_Task(void)
{
    uint32_t now = HAL_GetTick();

    Status_Task();

    if (led_active && (int32_t)(now - led_off_at) >= 0) {
        HAL_GPIO_WritePin(GPIOB, LED_GREEN_Pin | LED_RED_Pin, GPIO_PIN_RESET);
        led_active = 0;
    }

    if (beeps_left && (int32_t)(now - buzzer_next) >= 0) {
        if (buzzer_on) {
            HAL_GPIO_WritePin(GPIOB, BUZZER_Pin, GPIO_PIN_RESET);
            buzzer_on = 0;
            beeps_left--;
            buzzer_next = now + beep_off_ms;
        } else {
            HAL_GPIO_WritePin(GPIOB, BUZZER_Pin, GPIO_PIN_SET);
            buzzer_on = 1;
            buzzer_next = now + beep_on_ms;
        }
    }
}
