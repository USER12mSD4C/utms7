#ifndef COMMANDS_FS_H
#define COMMANDS_FS_H

int fs_commands_init(void);
const char* fs_get_current_dir(void);
void fs_set_current_dir(const char* path);

#endif
