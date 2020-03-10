#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define FIB_DEV "/dev/fibonacci"
#define FIB_TIME "/sys/kernel/fib_time/time"

#define bigN_base 100000000
#define bigN_num 8

typedef struct bigN_t {
    long long part[bigN_num];
} bigN;

int main()
{
    long long sz;

    bigN buf;
    char write_buf[] = "testing writing";
    char time_str[10];
    int offset = 100; /* TODO: try test something bigger than the limit */

    int fd = open(FIB_DEV, O_RDWR);
    if (fd < 0) {
        perror("Failed to open character device");
        exit(1);
    }


    for (int i = 0; i <= offset; i++) {
        sz = write(fd, write_buf, strlen(write_buf));
        printf("Writing to " FIB_DEV ", returned the sequence %lld\n", sz);
    }

    int fd_time;
    struct timespec start, end;
    FILE *file_user, *file_kernel, *file_kernel_user;
    char buffer[32];

    file_user = fopen("user_time.txt", "w+");
    file_kernel = fopen("kernel_time.txt", "w+");
    file_kernel_user = fopen("kernel_user_time.txt", "w+");
    for (int i = 0; i <= offset; i++) {
        lseek(fd, i, SEEK_SET);
        clock_gettime(CLOCK_MONOTONIC, &start);
        sz = read(fd, &buf, sizeof(bigN));
        clock_gettime(CLOCK_MONOTONIC, &end);

        fd_time = open(FIB_TIME, O_RDONLY);
        if (fd_time > 0) {
            memset(time_str, 0, 10);
            read(fd_time, time_str, 10);
        }
        printf("Reading from " FIB_DEV " at offset %d, returned the sequence ",
               i);
        for (int j = bigN_num - 1; j >= 0; j--)
            printf("%lld ", buf.part[j]);
        printf("\n");
        long user_time = end.tv_nsec - start.tv_nsec;
        int size = snprintf(buffer, sizeof(buffer), "%d %ld\n", i, user_time);
        fwrite(buffer, 1, size, file_user);
        size = snprintf(buffer, sizeof(buffer), "%d %s\n", i, time_str);
        fwrite(buffer, 1, size, file_kernel);
        size = snprintf(buffer, sizeof(buffer), "%d %d\n", i,
                        (int) user_time - atoi(time_str));
        fwrite(buffer, 1, size, file_kernel_user);
    }
    close(fd_time);
    fclose(file_user);
    fclose(file_kernel);
    fclose(file_kernel_user);

    for (int i = offset; i >= 0; i--) {
        lseek(fd, i, SEEK_SET);
        sz = read(fd, &buf, sizeof(bigN));
        printf("Reading from " FIB_DEV " at offset %d, returned the sequence ",
               i);
        for (int j = bigN_num - 1; j >= 0; j--)
            printf("%lld ", buf.part[j]);
        printf("\n");
    }

    close(fd);
    return 0;
}
