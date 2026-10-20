#ifndef ALERT_H
#define ALERT_H

#include <stdint.h>

/* Bat LED (2 giay) va coi theo nhip. led_pin: LED_GREEN_Pin hoac LED_RED_Pin. */
void Alert_Start(uint16_t led_pin, uint8_t beeps, uint16_t on_ms, uint16_t off_ms);
/* Nhay LED PC13 theo so xung khi doc the. */
void Alert_StatusBlink(uint8_t pulses, uint32_t half_period_ms);
/* Goi lien tuc trong while(1). */
void Alert_Task(void);

#endif /* ALERT_H */
