#ifndef USER_H
#define USER_H

#include <stdint.h>
#include <stdbool.h>

/* Trang thai he thong (may trang thai Admin) */
typedef enum {
    MODE_NORMAL = 0,  /* Diem danh binh thuong                       */
    MODE_ADMIN_IDLE,  /* Da quet the admin, cho bam nut              */
    MODE_ADMIN_ADD,   /* Bam nut 1: cho quet the de THEM             */
    MODE_ADMIN_DEL,   /* Bam nut 2: cho quet the de XOA              */
    MODE_ADMIN_FIND   /* Bam nut 3: cho quet the de TIM              */
} SystemMode;

/* Khoi tao sau khi card_db/OLED san sang. Goi 1 lan trong main(). */
void USER_Init(void);

/* Doc nut + xu ly timeout Admin + het thoi gian man ket qua. Goi lien tuc. */
void USER_Task(void);

/* Goi khi RC522 doc duoc 1 the moi. Phan phoi theo che do hien tai.
 * Tra ve true neu the duoc chap nhan (diem danh thanh cong hoac thao tac OK). */
bool USER_HandleCard(const uint8_t *uid, uint8_t uid_size);

SystemMode USER_GetMode(void);

#endif /* USER_H */
