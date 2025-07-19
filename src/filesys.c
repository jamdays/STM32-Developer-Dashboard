#include "filesys.h"
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>

// Print contents of root dir (/lfs) to Shell
static int cmd_ls (const struct shell *shell, size_t argc, char **argv) {
    int rc;
    struct fs_dir_t dir = {}; 
    static struct fs_dirent entry;


    rc = fs_opendir(&dir, "/lfs");
    if (rc < 0) {
        shell_error(shell, "Failed to open directory \"/\": %d", rc);
        return rc;
    }

    while(1) {
        rc = fs_readdir(&dir, &entry);
        if (rc < 0) {
            shell_error(shell, "Failed to read directory \"/\": %d", rc);
            fs_closedir(&dir);
            return rc;
        } else if (rc == 0 && entry.name[0] == '\0') {
            break; // No more entries
        } else if (rc == 0) {
            shell_print(shell, "%s", entry.name);
        } else {
            shell_print(shell, "Unexpected return value from fs_readdir: %d", rc);
            break;
        }
    }
    fs_closedir(&dir);
}

// Print contents of a file to Shell
static int cmd_cat(const struct shell *shell, size_t argc, char **argv)
{
    struct fs_file_t file;
    fs_file_t_init(&file);
    int rc;
    char * filepath = argv[1];
    char full_path[64];
    snprintf(full_path, sizeof(full_path), "/lfs/%s", filepath);

    rc = fs_open(&file, full_path, FS_O_READ);
    if (rc == -ENOENT) {
        shell_print(shell, "File doesn't exist: %s", filepath);
        return 0;
    } else if (rc < 0) {
        shell_error(shell, "Failed to open file %s: %d", filepath, rc);
        return rc;
    } else {
        shell_print(shell, "Contents of %s:", filepath);
        char buf[64];
        while(1) { // reading 64 bytes at a time for now. maybe bigger later
            int got = fs_read(&file, buf, sizeof(buf) - 1);
            if (got < 0) { //error
                shell_error(shell, "Failed to read file %s: %d", filepath, got);
                fs_close(&file);
                return got;
            } else if (got == 0) {
                break;
            } 
            buf[got] = '\0'; // null-terminate the string
            //shell_print(shell, "%s", buf); 
            shell_fprintf(shell, SHELL_NORMAL, "%s", buf);
        }
        fs_close(&file);
        return 0;
    }
}

void cmd_rm(const struct shell *shell, size_t argc, char **argv) {
    if (argc < 2) {
        shell_error(shell, "Usage: rm <file_name>");
        return;
    }
    const char *file_name = argv[1];
    int rc = fs_unlink(file_name);
    if (rc < 0) {
        shell_error(shell, "Failed to remove file %s: %d", file_name, rc);
    } else {
        shell_print(shell, "File %s removed successfully", file_name);
    }
}

SHELL_CMD_REGISTER(ls, NULL, "List items on FS", cmd_ls);
SHELL_CMD_REGISTER(cat, NULL, "Display contents of a file", cmd_cat);
SHELL_CMD_REGISTER(rm, NULL, "Remove a file", cmd_rm);