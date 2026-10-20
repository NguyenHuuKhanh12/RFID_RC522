#ifndef UI_H
#define UI_H

#include <stdbool.h>

/* Ve man hinh OLED: tieu de (dao mau), chu lon, 2 dong thong tin. */
void UI_Draw(const char *title, const char *big, const char *l1, const char *l2);
/* Ve ket qua tam thoi, sau ~2 giay UI_ResultExpired() tra ve true mot lan. */
void UI_ShowResult(const char *title, const char *big, const char *l1, const char *l2);
void UI_CancelResult(void);
bool UI_ResultExpired(void);

#endif /* UI_H */
