#include <stddef.h>
#include <zephyr/shell/shell.h>

#ifndef FILESYS_H
#define FILESYS_H


static int cmd_ls(const struct shell *shell, size_t argc, char **argv); 


static int cmd_cat(const struct shell *shell, size_t argc, char **argv);

void cmd_rm(const struct shell *shell, size_t argc, char **argv); 

#endif