// userapp.c - CS423 MP1 test app
// Registers its PID with /proc/mp1/status, does busy work for ~10-15s,
// then reads /proc/mp1/status and prints it.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#define PROC_STATUS_PATH "/proc/mp1/status"

static void error_exit(const char *msg)
{
    fprintf(stderr, "userapp: %s: %s\n", msg, strerror(errno));
    exit(1);
}

/* Register the current process's PID with the kernel module */
static void register_self_pid(void)
{
    pid_t pid = getpid();

    FILE *fp = fopen(PROC_STATUS_PATH, "w");
    if (!fp)
        error_exit("failed to open " PROC_STATUS_PATH " for write");

    // Write the PID to the file, followed by a newline. The kernel module will read this and add it to its tracking list.
    if (fprintf(fp, "%d\n", pid) < 0) { 
        fclose(fp);
        error_exit("failed to write pid to " PROC_STATUS_PATH);
    }

    if (fclose(fp) != 0)
        error_exit("failed to close " PROC_STATUS_PATH " after write");
}

/* Print the contents of the /proc/mp1/status file */
static void print_status_file(void)
{
    FILE *fp = fopen(PROC_STATUS_PATH, "r");
    if (!fp)
        error_exit("failed to open " PROC_STATUS_PATH " for read");

    // Read the file character by character and print it to stdout. This will show the list of tracked PIDs and their CPU usage as maintained by the kernel module.
    int c;
    while ((c = getc(fp)) != EOF)
        putchar(c);

    if (ferror(fp)) {
        fclose(fp);
        error_exit("error while reading " PROC_STATUS_PATH);
    }

    if (fclose(fp) != 0)
        error_exit("failed to close " PROC_STATUS_PATH " after read");
}

int main(void)
{
    register_self_pid();

    // Busy work to consume CPU time for ~10-15 seconds.
    volatile unsigned long long sum = 0;
    for (int i = 0; i < 100000000; i++) {
        volatile unsigned long long fac = 1;
        for (int j = 1; j <= 50; j++) {
            fac *= (unsigned long long)j;
        }
        sum += fac;
    }
    (void)sum;

    print_status_file();
    return 0;
}
