/**
 * \file ui-player.h
 * \brief character info
 */

#ifndef UI_PLAYER_H
#define UI_PLAYER_H

#include "ui-event.h"

extern bool arg_force_name;

char *fmt_weight(int grams, char *buf);
void display_player_stat_info(bool);
void display_player_xtra_info(bool);
void display_player(int mode);
void write_character_dump(ang_file *fff);
bool dump_save(const char *path);
void do_cmd_change_name(void);
const char *player_title(void);
ui_event ui_text_box(const char *text);

#endif /* !UI_PLAYER_H */
