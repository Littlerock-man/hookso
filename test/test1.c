#include <stdio.h>
#include <unistd.h>
#include <time.h>

extern void hello();

int main() {
    while (1) {
        hello();
        sleep(1);
    }

    return 0;
}
