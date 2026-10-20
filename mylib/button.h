#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>

#define BUTTON_ADD   (1U << 0)
#define BUTTON_DEL   (1U << 1)
#define BUTTON_FIND  (1U << 2)

/* Goi moi vong lap. Tra ve bitmask cac nut vua duoc nhan (suon xuong, da chong doi). */
uint8_t Button_Scan(void);

#endif /* BUTTON_H */
