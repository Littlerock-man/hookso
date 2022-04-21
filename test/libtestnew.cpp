#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string>
#include <list>
#include <vector>
#include <map>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <string>
#include <cstdlib>
#include <cstring>
#include <typeinfo>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <assert.h>
#include <math.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <unordered_map>
#include <fcntl.h>
#include <sstream>
#include <algorithm>
#include <vector>
#include <unordered_set>
#include <set>
#include <unistd.h>
#include <linux/limits.h>
#include <inttypes.h>
#include <elf.h>
#include <sys/uio.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <sys/stat.h>

#define PROCMAPS_LINE_MAX_LENGTH (PATH_MAX + 100)
uint64_t get_mem_addr(pid_t pid) {
	std::vector<std::pair<uint64_t, uint64_t>> mapping;
    mapping.clear();

    char maps_path[PATH_MAX];
    sprintf(maps_path, "/proc/%d/maps", pid);
    FILE *fd = fopen(maps_path, "r");
    if (!fd) {
        printf("cannot open the memory maps, %s", strerror(errno));
        return -1;
    }

    char buf[PROCMAPS_LINE_MAX_LENGTH];
    while (!feof(fd)) {
        if (fgets(buf, PROCMAPS_LINE_MAX_LENGTH, fd) == NULL) {
            break;
        }

        std::vector<std::string> tmp;

        const char *sep = "\t \r\n";
        char *line = NULL;
        for (char *token = strtok_r(buf, sep, &line); token != NULL; token = strtok_r(NULL, sep, &line)) {
            tmp.push_back(token);
        }

        if (tmp.empty()) {
            continue;
        }

        std::string range = tmp[0];
        int pos = range.find_last_of("-");
        if (pos == -1) {
            continue;
        }

        std::string beginstr = range.substr(0, pos);
        std::string endstr = range.substr(pos + 1);

        uint64_t begin = (uint64_t) std::strtoul(beginstr.c_str(), 0, 16);
        uint64_t end = (uint64_t) std::strtoul(endstr.c_str(), 0, 16);
        mapping.push_back(std::make_pair(begin, end));
    }

    fclose(fd);

    return mapping[0].first;
}

extern "C" bool libprintprocess_memory() {
    char maps_path[100];

    pid_t pid = getpid();
    sprintf(maps_path, "/proc/%d/maps", pid);

    printf("pid=%d, mem addr=0x%lx\n", pid, get_mem_addr(pid));

    return false;

}

extern "C" bool libtestnew(int n) { char buff[128] = {0};
    snprintf(buff, sizeof(buff), "libtestnew %d", n);
    puts(buff);
    return false;
}

extern "C" bool putsnew(const char *str) {
    char buff[128] = {0};
    snprintf(buff, sizeof(buff), "putsnew %s", str);
    puts(buff);
    return false;
}

extern "C" void mysleepnew() {
    puts("mysleepnew");
    sleep(1);
}

extern "C" void hello() {
    puts("hello world");
}

