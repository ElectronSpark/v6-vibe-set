#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    int fd0 = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    int fd1 = open("/dev/input/event1", O_RDONLY | O_NONBLOCK);

    printf("kde_libinput_probe event0=%d event1=%d\n", fd0, fd1);
    if (fd0 >= 0)
        close(fd0);
    if (fd1 >= 0)
        close(fd1);
    return (fd0 >= 0 || fd1 >= 0) ? 0 : 1;
}
