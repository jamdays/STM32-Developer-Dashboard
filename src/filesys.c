#include "filesys.h"
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/fs/fs_interface.h>
#include <zephyr/storage/flash_map.h>
#include<string.h>

char current_dir[256];
//struct fs_dir_t *current_dir_struct;

void set_dir(const char *path)
{
    strcpy(current_dir, path);
}


void init_dir(){
    set_dir("/lfs");
    //current_dir_struct = malloc(sizeof(struct fs_dir_t));
    //fs_dir_t_init(current_dir_struct);
}

// Print contents of current dir to Shell
static int cmd_ls (const struct shell *shell, size_t argc, char **argv) {

    int rc;
    struct fs_dir_t dir;
    fs_dir_t_init(&dir);
    static struct fs_dirent entry;

    rc = fs_opendir(&dir, current_dir);
    if (rc < 0) {
        shell_error(shell, "Failed to open directory \"%s\": %d", current_dir, rc);
        return rc;
    }

    while (1) {
        rc = fs_readdir(&dir, &entry);
        if (rc < 0) {
            shell_error(shell, "Failed to read directory \"%s\": %d", current_dir, rc);
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
    return 0;
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
    //const char *file_name = argv[1];
    char * filepath = argv[1];
    char file_name[256];
    if (strcmp(current_dir, "/") == 0)
        snprintf(file_name, sizeof(file_name), "/%s", filepath);
    else
        snprintf(file_name, sizeof(file_name), "%s/%s", current_dir, filepath);
    int rc = fs_unlink(file_name);
    if (rc < 0) {
        shell_error(shell, "Failed to remove file %s: %d", file_name, rc);
    } else {
        shell_print(shell, "File %s removed successfully", file_name);
    }
}

void cmd_mkdir(const struct shell *shell, size_t argc, char **argv){

    int rc;

    if (argc != 2) {
        shell_error(shell, "Usage: mkdir <dir_name>");
        return;
    }

    //path to create:
    char new_path[256];

    if (strcmp(current_dir, "/") == 0)
        snprintf(new_path, sizeof(new_path), "/%s", argv[1]);
    else
        snprintf(new_path, sizeof(new_path), "%s/%s", current_dir, argv[1]);


    rc = fs_mkdir(new_path);

    if(rc == -EEXIST){
        shell_error(shell, "Directory already exists: %s", new_path);
        return;
    } 

    else if(rc < 0){
        shell_error(shell, "Error in making new Directory", new_path);
    }

    return;
}

void cmd_pwd(const struct shell *shell, size_t argc, char **argv){
    shell_print(shell, "%s\n", current_dir);
    return;
}

void cmd_cd(const struct shell *shell, size_t argc, char **argv){

    struct fs_dir_t test_dir;
    fs_dir_t_init(&test_dir);

    char new_path[256];
    char *dest = argv[1];

    if (argc != 2) {
        shell_error(shell, "Usage: cd <dir_name>");
        return;
    }

    // if the path given is .. then change to the parent directory
    if(strcmp(argv[1], "..") == 0){
        if(strcmp(current_dir, "/lfs") == 0){
            shell_error(shell, "Presently in root directory");
        }

        else{
            char *last = strrchr(current_dir, '/');
            *last = '\0';
        }
    }

    // then path is current_dir + <dir_name>
    else{

        if (strcmp(current_dir, "/lfs") == 0){
            snprintf(new_path, sizeof(new_path), "/lfs/%s", dest);
        }else{
            snprintf(new_path, sizeof(new_path), "%s/%s", current_dir, dest);
        }

        // check if dir_name is in current directory
        int rc = fs_opendir(&test_dir, new_path);

        if (rc < 0) {
            shell_error(shell, "Directory not found: %s", new_path);
            return;
        }

        fs_closedir(&test_dir);

        strcpy(current_dir, new_path);
    }

    shell_print(shell, "Now in: %s", current_dir);
}



SHELL_CMD_REGISTER(cd, NULL, "Change directory", cmd_cd);
SHELL_CMD_REGISTER(pwd, NULL, "present working directory", cmd_pwd);
SHELL_CMD_REGISTER(mkdir, NULL, "Create directory", cmd_mkdir);
SHELL_CMD_REGISTER(ls, NULL, "List items in directory", cmd_ls);
SHELL_CMD_REGISTER(cat, NULL, "Display contents of a file", cmd_cat);
SHELL_CMD_REGISTER(rm, NULL, "Remove a file", cmd_rm);