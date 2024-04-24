#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sched.h>
#include <fcntl.h>
#include <errno.h>
#define STACK_SIZE 1048576

int ASSERT(int status, const char *msg) {
    if (status < 0) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
    return status;
}

void set_host_name(const char* hostname) {
    ASSERT(sethostname(hostname, strlen(hostname)) >= 0, "Failed to call sethostname function");
}

void setup_variables() {
    clearenv(); // remove all environment variables for this process
    setenv("TERM", "xterm-256color", 1);
    setenv("PATH", "/bin/:/sbin:/usr/bin:/usr/sbin", 1);
}

void run_system(const char* cmd) {
    int status = system(cmd);
    ASSERT(status == 0, "Failed to run command");
}

char* run_system_with_buffer(const char* cmd, size_t buffer_size) {
    FILE* pipe = popen(cmd, "r");
    ASSERT(pipe != NULL, "Failed to run command");
    char* buffer = malloc(buffer_size);
    ASSERT(fgets(buffer, buffer_size, pipe) != NULL, "Failed to call fgets command");
    ASSERT(pclose(pipe) == 0, "Child process failed with non zero code");
    return buffer;
}

char* setup_filesystem() {
    int has_loopback = access("loopbackfile.img", F_OK) == 0;
    if (!has_loopback) {
        printf("[DEBUG] Creating loopback file loopbackfile.img (1GB)\n");
        run_system("dd if=/dev/zero of=loopbackfile.img bs=100M count=10 >/dev/null 2>/dev/null");
    }

    char* loop_device = run_system_with_buffer("losetup -f --show loopbackfile.img", 64);
    loop_device[strcspn(loop_device, "\n")] = 0; // Remove trailing newline
    printf("[DEBUG] Mounted loop device: %s\n", loop_device);

    if (!has_loopback) {
        printf("[DEBUG] Creating ext4 filesystem on loopbackfile.img\n");
        run_system("mkfs.ext4 loopbackfile.img >/dev/null 2>/dev/null");
    }

    printf("[DEBUG] Creating temp folder to mount filesystem: ./mnt\n");
    ASSERT(mkdir("mnt", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0, "Failed to create directory");

    printf("[DEBUG] Mounting loopback device to the temp folder\n");
    char cmd[256];
    sprintf(cmd, "mount -o loop %s mnt", loop_device);
    run_system(cmd);

    if (!has_loopback) {
        printf("[DEBUG] Downloading and installing alpine linux\n");
        run_system("wget -qO- https://nl.alpinelinux.org/alpine/v3.16/releases/x86_64/alpine-minirootfs-3.16.3-x86_64.tar.gz | tar xvz -C mnt > /dev/null");

    }
    
  
    printf("[DEBUG] The file system has been configured\n");

    return loop_device;
}

void unsetup_filesystem(const char* loop_device) {
    printf("[DEBUG] Unmounting loopback device and remove temp folder\n");
    run_system("umount mnt");
    ASSERT(rmdir("mnt") == 0, "Failed to remove directory");
    printf("[DEBUG] Unmounting loop device: %s\n", loop_device);
    char cmd[256];
    sprintf(cmd, "losetup -d %s", loop_device);
    run_system(cmd);
}

int main_container(void* args) {
    char* exec_args[] = {"/bin/sh", NULL};
    return execvp("/bin/sh", exec_args);
}

int child_fn(void* args) {
    set_host_name("container");
    setup_variables();

    ASSERT(chroot("mnt") == 0, "Failed to chroot");
    ASSERT(chdir("/") == 0, "Failed to chdir");

    ASSERT(mount("proc", "/proc", "proc", 0, NULL) == 0, "Failed to mount proc");
    
    
    char stack[STACK_SIZE];
    if (clone(main_container, stack + STACK_SIZE, SIGCHLD, NULL) < 0) {
        perror("Failed to create container. Try to run with root privileges");
        exit(EXIT_FAILURE);
    }
    wait(NULL);

    ASSERT(umount("/proc") == 0, "Failed to unmount proc");

    return EXIT_SUCCESS;
}

int main() {
    printf("Welcome to My linux container!\n");

    char* loop_device = setup_filesystem();

    char stack[STACK_SIZE];
    if (clone(child_fn, stack + STACK_SIZE, CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNET | CLONE_NEWNS | SIGCHLD, NULL) < 0) {
        perror("Failed to create container. Try to run with root privileges");
        exit(EXIT_FAILURE);
    }
    wait(NULL);

    unsetup_filesystem(loop_device);

    return EXIT_SUCCESS;
}

