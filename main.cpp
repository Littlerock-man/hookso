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

#define PROCMAPS_LINE_MAX_LENGTH (PATH_MAX + 100)

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#define LOG(...) log(stdout, "[DEBUG]", __FILENAME__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define ERR(...) log(stderr, "[ERROR]", __FILENAME__, __FUNCTION__, __LINE__, __VA_ARGS__)

void log(FILE *fd, const char *header, const char *file, const char *func, int pos, const char *fmt, ...) {
    time_t clock1;
    struct tm *tptr;
    va_list ap;

    clock1 = time(0);
    tptr = localtime(&clock1);

    struct timeval tv;
    gettimeofday(&tv, NULL);

    fprintf(fd, "%s[%d.%d.%d,%d:%d:%d,%llu]%s:%d,%s: ", header,
            tptr->tm_year + 1900, tptr->tm_mon + 1,
            tptr->tm_mday, tptr->tm_hour, tptr->tm_min,
            tptr->tm_sec, (long long) ((tv.tv_usec) / 1000) % 1000, file, pos, func);

    va_start(ap, fmt);
    vfprintf(fd, fmt, ap);
    fprintf(fd, "\n");
    va_end(ap);
}

int remote_process_vm_readv(pid_t remote_pid, void *address, void *buffer, size_t len) {
    struct iovec local[1] = {};
    struct iovec remote[1] = {};
    int errsv = 0;
    ssize_t nread = 0;

    local[0].iov_len = len;
    local[0].iov_base = (void *) buffer;

    remote[0].iov_base = address;
    remote[0].iov_len = local[0].iov_len;

    nread = process_vm_readv(remote_pid, local, 1, remote, 1, 0);

    if (nread != (int) local[0].iov_len) {
        errsv = errno;
        return errsv;
    }

    return 0;
}

int remote_process_ptrace_read(pid_t remote_pid, void *address, void *buffer, size_t len) {
    int errsv = 0;

    char file[PATH_MAX];
    sprintf(file, "/proc/%d/mem", remote_pid);
    int fd = open(file, O_RDWR);
    if (fd < 0) {
        errsv = errno;
        return errsv;
    }

    int ret = ptrace(PTRACE_ATTACH, remote_pid, 0, 0);
    if (ret < 0) {
        errsv = errno;
        return errsv;
    }

    ret = waitpid(remote_pid, NULL, 0);
    if (ret < 0) {
        errsv = errno;
        ptrace(PTRACE_DETACH, remote_pid, 0, 0);
        return errsv;
    }

    ret = pread(fd, buffer, len, (off_t) address);
    if (ret < 0) {
        errsv = errno;
        ptrace(PTRACE_DETACH, remote_pid, 0, 0);
        return errsv;
    }

    ret = close(fd);
    if (ret < 0) {
        errsv = errno;
        return errsv;
    }

    ret = ptrace(PTRACE_DETACH, remote_pid, 0, 0);
    if (ret < 0) {
        errsv = errno;
        return errsv;
    }

    return 0;
}

int remote_process_ptrace_word_read(pid_t remote_pid, void *address, void *buffer, size_t len) {
    int errsv = 0;

    int ret = ptrace(PTRACE_ATTACH, remote_pid, 0, 0);
    if (ret < 0) {
        errsv = errno;
        return errsv;
    }

    ret = waitpid(remote_pid, NULL, 0);
    if (ret < 0) {
        errsv = errno;
        ptrace(PTRACE_DETACH, remote_pid, 0, 0);
        return errsv;
    }

    for (int i = 0; i < ((int) len + 3) / 4; ++i) {
        errno = 0;
        ret = ptrace(PTRACE_PEEKTEXT, remote_pid, (char *) address + (i * 4), 0);
        errsv = errno;
        if (errsv != 0) {
            ptrace(PTRACE_DETACH, remote_pid, 0, 0);
            return errsv;
        }
        for (int j = 0; j < 4 && (i * 4) + j < (int) len; ++j) {
            ((char *) buffer)[(i * 4) + j] = ((char *) &ret)[j];
        }
    }

    ret = ptrace(PTRACE_DETACH, remote_pid, 0, 0);
    if (ret < 0) {
        errsv = errno;
        return errsv;
    }

    return 0;
}

int remote_process_read(pid_t remote_pid, void *address, void *buffer, size_t len) {
    int ret = 0;
    ret = remote_process_vm_readv(remote_pid, address, buffer, len);
    if (ret == 0) {
        return ret;
    }
    ret = remote_process_ptrace_read(remote_pid, address, buffer, len);
    if (ret == 0) {
        return ret;
    }
    ret = remote_process_ptrace_word_read(remote_pid, address, buffer, len);
    if (ret == 0) {
        return ret;
    }
    ERR("hookplt: remote_process_read fail %d %s", ret, strerror(ret));
    return ret;
}

int find_so_func_addr(pid_t pid, const std::string &soname,
                      const std::string &funcname,
                      uint64_t &funcaddr_plt_offset, void *&funcaddr) {

    char maps_path[PATH_MAX];
    sprintf(maps_path, "/proc/%d/maps", pid);
    FILE *fd = fopen(maps_path, "r");
    if (!fd) {
        ERR("hookplt: cannot open the memory maps, %s", strerror(errno));
        return -1;
    }

    std::string sobeginstr;
    char buf[PROCMAPS_LINE_MAX_LENGTH];
    while (!feof(fd)) {
        if (fgets(buf, PROCMAPS_LINE_MAX_LENGTH, fd) == NULL) {
            break;
        }

        std::vector <std::string> tmp;

        const char *sep = "\t \r\n";
        char *line = NULL;
        for (char *token = strtok_r(buf, sep, &line); token != NULL; token = strtok_r(NULL, sep, &line)) {
            tmp.push_back(token);
        }

        if (tmp.empty()) {
            continue;
        }

        std::string path = tmp[tmp.size() - 1];
        if (path == "(deleted)") {
            if (tmp.size() < 2) {
                continue;
            }
            path = tmp[tmp.size() - 2];
        }

        int pos = path.find_last_of("/");
        if (pos == -1) {
            continue;
        }
        std::string targetso = path.substr(pos + 1);
        targetso.erase(std::find_if(targetso.rbegin(), targetso.rend(), [](int ch) {
            return !std::isspace(ch);
        }).base(), targetso.end());
        if (targetso == soname) {
            sobeginstr = tmp[0];
            pos = sobeginstr.find_last_of("-");
            if (pos == -1) {
                ERR("hookplt: parse /proc/%d/maps %s fail", pid, soname.c_str());
                return -1;
            }
            sobeginstr = sobeginstr.substr(0, pos);
            break;
        }
    }

    fclose(fd);

    if (sobeginstr.empty()) {
        ERR("hookplt: find /proc/%d/maps %s fail", pid, soname.c_str());
        return -1;
    }

    uint64_t sobeginvalue = std::strtoul(sobeginstr.c_str(), 0, 16);

    LOG("find target so, begin with 0x%s %lu", sobeginstr.c_str(), sobeginvalue);

    Elf64_Ehdr targetso;
    int ret = remote_process_read(pid, (void *) sobeginvalue, &targetso, sizeof(targetso));
    if (ret != 0) {
        return -1;
    }

    if (targetso.e_ident[EI_MAG0] != ELFMAG0 ||
        targetso.e_ident[EI_MAG1] != ELFMAG1 ||
        targetso.e_ident[EI_MAG2] != ELFMAG2 ||
        targetso.e_ident[EI_MAG3] != ELFMAG3) {
        ERR("hookplt: not valid elf header /proc/%d/maps %lu ", pid, sobeginvalue);
        return -1;
    }

    LOG("read head ok %lu", sobeginvalue);
    LOG("section offset %lu", targetso.e_shoff);
    LOG("section num %d", targetso.e_shnum);
    LOG("section size %d", targetso.e_shentsize);
    LOG("section header string table index %d", targetso.e_shstrndx);

    Elf64_Shdr setions[targetso.e_shnum];
    ret = remote_process_read(pid, (void *) (sobeginvalue + targetso.e_shoff), &setions, sizeof(setions));
    if (ret != 0) {
        return -1;
    }

    Elf64_Shdr &shsection = setions[targetso.e_shstrndx];
    LOG("section header string table offset %ld", shsection.sh_offset);
    LOG("section header string table size %ld", shsection.sh_size);

    char shsectionname[shsection.sh_size];
    ret = remote_process_read(pid, (void *) (sobeginvalue + shsection.sh_offset), shsectionname, sizeof(shsectionname));
    if (ret != 0) {
        return -1;
    }

    int pltindex = -1;
    int dynsymindex = -1;
    int dynstrindex = -1;
    int relapltindex = -1;
    for (int i = 0; i < targetso.e_shnum; ++i) {
        Elf64_Shdr &s = setions[i];
        std::string name = &shsectionname[s.sh_name];
        if (name == ".plt") {
            pltindex = i;
        }
        if (name == ".dynsym") {
            dynsymindex = i;
        }
        if (name == ".dynstr") {
            dynstrindex = i;
        }
        if (name == ".rela.plt") {
            relapltindex = i;
        }
    }

    if (pltindex < 0) {
        ERR("hookplt: not find .plt %s", soname.c_str());
        return -1;
    }
    if (dynsymindex < 0) {
        ERR("hookplt: not find .dynsym %s", soname.c_str());
        return -1;
    }
    if (dynstrindex < 0) {
        ERR("hookplt: not find .dynstr %s", soname.c_str());
        return -1;
    }
    if (relapltindex < 0) {
        ERR("hookplt: not find .rel.plt %s", soname.c_str());
        return -1;
    }

    Elf64_Shdr &pltsection = setions[pltindex];
    LOG("plt index %d", pltindex);
    LOG("plt section offset %ld", pltsection.sh_offset);
    LOG("plt section size %ld", pltsection.sh_size);

    Elf64_Shdr &dynsymsection = setions[dynsymindex];
    LOG("dynsym index %d", dynsymindex);
    LOG("dynsym section offset %ld", dynsymsection.sh_offset);
    LOG("dynsym section size %ld", dynsymsection.sh_size / sizeof(Elf64_Sym));

    Elf64_Sym sym[dynsymsection.sh_size / sizeof(Elf64_Sym)];
    ret = remote_process_read(pid, (void *) (sobeginvalue + dynsymsection.sh_offset), &sym, sizeof(sym));
    if (ret != 0) {
        return -1;
    }

    Elf64_Shdr &dynstrsection = setions[dynstrindex];
    LOG("dynstr index %d", dynstrindex);
    LOG("dynstr section offset %ld", dynstrsection.sh_offset);
    LOG("dynstr section size %ld", dynstrsection.sh_size);

    char dynstr[dynstrsection.sh_size];
    ret = remote_process_read(pid, (void *) (sobeginvalue + dynstrsection.sh_offset), dynstr, sizeof(dynstr));
    if (ret != 0) {
        return -1;
    }

    int symfuncindex = -1;
    for (int j = 0; j < (int) (dynsymsection.sh_size / sizeof(Elf64_Sym)); ++j) {
        Elf64_Sym &s = sym[j];
        std::string name = &dynstr[s.st_name];
        if (name == funcname) {
            symfuncindex = j;
            break;
        }
    }

    if (symfuncindex < 0) {
        ERR("hookplt: not find %s in .dynsym %s", funcname.c_str(), soname.c_str());
        return -1;
    }

    Elf64_Sym &targetsym = sym[symfuncindex];
    if (targetsym.st_shndx != SHN_UNDEF && targetsym.st_value != 0 && targetsym.st_size != 0) {
        Elf64_Shdr &s = setions[targetsym.st_shndx];
        std::string name = &shsectionname[s.sh_name];
        if (name == ".text") {
            void *func = (void *) (sobeginvalue + targetsym.st_value);
            LOG("target text func addr %p", func);
            funcaddr_plt_offset = 0;
            funcaddr = func;
            return 0;
        }
    }

    Elf64_Shdr &relapltsection = setions[relapltindex];
    LOG("relaplt index %d", relapltindex);
    LOG("relaplt section offset %ld", relapltsection.sh_offset);
    LOG("relaplt section size %ld", relapltsection.sh_size / sizeof(Elf64_Rela));

    Elf64_Rela rela[relapltsection.sh_size / sizeof(Elf64_Rela)];
    ret = remote_process_read(pid, (void *) (sobeginvalue + relapltsection.sh_offset), &rela, sizeof(rela));
    if (ret != 0) {
        return -1;
    }

    int relafuncindex = -1;
    for (int j = 0; j < (int) (relapltsection.sh_size / sizeof(Elf64_Rela)); ++j) {
        Elf64_Rela &r = rela[j];
        if ((int) ELF64_R_SYM(r.r_info) == symfuncindex) {
            relafuncindex = j;
            break;
        }
    }

    if (relafuncindex < 0) {
        ERR("hookplt: not find %s in .rela.plt %s", funcname.c_str(), soname.c_str());
        return -1;
    }

    Elf64_Rela &relafunc = rela[relafuncindex];
    LOG("target rela index %d", relafuncindex);
    LOG("target rela addr %ld", relafunc.r_offset);

    void *func;
    ret = remote_process_read(pid, (void *) (sobeginvalue + relafunc.r_offset), &func, sizeof(func));
    if (ret != 0) {
        return -1;
    }

    LOG("target got.plt func old addr %p", func);

    funcaddr_plt_offset = relafunc.r_offset;
    funcaddr = func;

    return 0;
}

std::string find_libc_name(pid_t pid) {

    char maps_path[PATH_MAX];
    sprintf(maps_path, "/proc/%d/maps", pid);
    FILE *fd = fopen(maps_path, "r");
    if (!fd) {
        ERR("hookplt: cannot open the memory maps, %s", strerror(errno));
        return "";
    }
//
//    std::string sobeginstr;
//    char buf[PROCMAPS_LINE_MAX_LENGTH];
//    while (!feof(fd)) {
//        if (fgets(buf, PROCMAPS_LINE_MAX_LENGTH, fd) == NULL) {
//            break;
//        }
//
//        std::vector <std::string> tmp;
//
//        const char *sep = "\t \r\n";
//        char *line = NULL;
//        for (char *token = strtok_r(buf, sep, &line); token != NULL; token = strtok_r(NULL, sep, &line)) {
//            tmp.push_back(token);
//        }
//
//        if (tmp.empty()) {
//            continue;
//        }
//
//        std::string path = tmp[tmp.size() - 1];
//        if (path == "(deleted)") {
//            if (tmp.size() < 2) {
//                continue;
//            }
//            path = tmp[tmp.size() - 2];
//        }
//
//        int pos = path.find_last_of("/");
//        if (pos == -1) {
//            continue;
//        }
//        std::string targetso = path.substr(pos + 1);
//        targetso.erase(std::find_if(targetso.rbegin(), targetso.rend(), [](int ch) {
//            return !std::isspace(ch);
//        }).base(), targetso.end());
//        if (targetso == soname) {
//            sobeginstr = tmp[0];
//            pos = sobeginstr.find_last_of("-");
//            if (pos == -1) {
//                ERR("hookplt: parse /proc/%d/maps %s fail",pid, soname.c_str());
//                return -1;
//            }
//            sobeginstr = sobeginstr.substr(0, pos);
//            break;
//        }
//    }

    fclose(fd);

    return "";
}

int inject_so(pid_t pid, const std::string &sopath) {

    int ret = ptrace(PTRACE_ATTACH, pid, 0, 0);
    if (ret < 0) {
        ERR("hookplt: inject_so fail %d %s ptrace PTRACE_ATTACH", pid, sopath.c_str());
        return -1;
    }

    ret = waitpid(pid, NULL, 0);
    if (ret < 0) {
        ERR("hookplt: inject_so fail %d %s waitpid ATTACH", pid, sopath.c_str());
        ptrace(PTRACE_DETACH, pid, 0, 0);
        return -1;
    }

    return 0;
}

int usage() {
    printf("\n"
           "hookso: type params\n"
           "\n"
           "eg:\n"
           "\n"
           "./hookso replace pid src-so srcfunc target-so-path target-func\n"
    );
    return -1;
}

int replace(int argc, char **argv) {

    if (argc < 6) {
        return usage();
    }

    std::string pidstr = argv[2];
    std::string srcso = argv[3];
    std::string srcfunc = argv[4];
    std::string targetso = argv[5];
    std::string targetfunc = argv[6];

    LOG("pid=%s", pidstr.c_str());
    LOG("src so=%s", srcso.c_str());
    LOG("src function=%s", srcfunc.c_str());
    LOG("target so=%s", targetso.c_str());
    LOG("target function=%s", targetfunc.c_str());

    LOG("start parse so file %s %s", srcso.c_str(), srcfunc.c_str());

    int pid = atoi(pidstr.c_str());

    uint64_t old_funcaddr_plt_offset = 0;
    void *old_funcaddr = 0;
    int ret = find_so_func_addr(pid, srcso.c_str(), srcfunc.c_str(), old_funcaddr_plt_offset, old_funcaddr);
    if (ret != 0) {
        return -1;
    }

    LOG("%s old %s %p offset %lu", srcso.c_str(), srcfunc.c_str(), old_funcaddr, old_funcaddr_plt_offset);

    LOG("start inject so file %s", targetso.c_str());

    ret = inject_so(pid, targetso);
    if (ret != 0) {
        return -1;
    }

    return 0;
}

int main(int argc, char **argv) {

    if (argc < 2) {
        return usage();
    }

    std::string type = argv[1];

    if (type == "replace") {
        return replace(argc, argv);
    } else {
        return usage();
    }
}
