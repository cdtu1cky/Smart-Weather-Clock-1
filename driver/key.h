#ifndef __KEY_H__
#define __KEY_H__


#include <stdbool.h>


void key_init(void);

/* 扫描按键，检测到一次按下返回 true（内部做了 20ms 消抖） */
bool key_scan(void);


#endif /* __KEY_H__ */
