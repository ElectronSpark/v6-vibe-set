#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
    printf("kde_abi_probe pid=%ld xdg=%s desktop=%s qpa=%s\n",
           (long)getpid(),
           getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "",
           getenv("XDG_CURRENT_DESKTOP") ? getenv("XDG_CURRENT_DESKTOP") : "",
           getenv("QT_QPA_PLATFORM") ? getenv("QT_QPA_PLATFORM") : "");
    return 0;
}
