/* tools/mkbootimg/mkbootimg.c
**
** Copyright 2007, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "mincrypt/sha.h"
#include "bootimg.h"

static void *load_file(const char *fn, unsigned *_sz)
{
    char *data;
    int sz;
    int fd;

    data = 0;
    fd = open(fn, O_RDONLY);
    if(fd < 0) return 0;

    sz = lseek(fd, 0, SEEK_END);
    if(sz < 0) goto oops;

    if(lseek(fd, 0, SEEK_SET) != 0) goto oops;

    data = (char*) malloc(sz);
    if(data == 0) goto oops;

    if(read(fd, data, sz) != sz) goto oops;
    close(fd);

    if(_sz) *_sz = sz;
    return data;

oops:
    close(fd);
    if(data != 0) free(data);
    return 0;
}

int usage(void)
{
    fprintf(stderr,"usage: mkbootimg\n"
            "       --kernel <filename>\n"
            "       --ramdisk <filename>\n"
            "       [ --second <2ndbootloader-filename> ]\n"
            "       [ --cmdline <kernel-commandline> ]\n"
            "       [ --board <boardname> ]\n"
            "       [ --base <address> ]\n"
            "       [ --pagesize <pagesize> ]\n"
            "       [ --ramdiskaddr <address> ]\n"
            "       [ --ot <boot|recovery>\n"
            "       -o|--output <filename>\n"
            );
    return 1;
}



static unsigned char padding[2048] = { 0, };

int write_padding(int fd, unsigned pagesize, unsigned itemsize)
{
    unsigned pagemask = pagesize - 1;
    unsigned count;

    if((itemsize & pagemask) == 0) {
        return 0;
    }

    count = pagesize - (itemsize & pagemask);

    if(write(fd, padding, count) != (signed)count) {
        return -1;
    } else {
        return 0;
    }
}

int main(int argc, char **argv)
{
    boot_img_hdr hdr;
    
    mt6516_kernel_hdr kernel_hdr;
    mt6516_rootfs_hdr rootfs_hdr;
    mt6516_recovery_hdr recovery_hdr;
    
    memcpy(kernel_hdr.magic_number, MT6516_MAGIC_NUMBER, 4);
    memcpy(kernel_hdr.magic,KERNEL_MAGIC, KERNEL_MAGIC_SIZE);
    memset(kernel_hdr.zero, 0,MT6516_ZERO_SIZE );
    memset(kernel_hdr.ff, 0xff,MT6516_FF_SIZE );
    
    memcpy(rootfs_hdr.magic_number, MT6516_MAGIC_NUMBER, 4);
    memcpy(rootfs_hdr.magic,ROOTFS_MAGIC, ROOTFS_MAGIC_SIZE);
    memset(rootfs_hdr.zero, 0,MT6516_ZERO_SIZE );
    memset(rootfs_hdr.ff, 0xff,MT6516_FF_SIZE );
    
    memcpy(recovery_hdr.magic_number, MT6516_MAGIC_NUMBER, 4);
    memcpy(recovery_hdr.magic,RECOVERY_MAGIC, RECOVERY_MAGIC_SIZE);
    memset(recovery_hdr.zero, 0,MT6516_ZERO_SIZE );
    memset(recovery_hdr.ff, 0xff,MT6516_FF_SIZE );


    char *kernel_fn = 0;
    char *kernel_mt = 0;
    void *kernel_data = 0;
    char *ramdisk_fn = 0;
    char *ramdisk_mt = 0;
    void *ramdisk_data = 0;
    char *second_fn = 0;
    void *second_data = 0;
    char *cmdline = "";
    char *bootimg = 0;
    char *board = "";
    char *out_type = 0;
    unsigned pagesize = 2048;
    int fd;
    int fd1;
    SHA_CTX ctx;
    const uint8_t* sha;

    argc--;
    argv++;

    memset(&hdr, 0, sizeof(hdr));

        /* default load addresses */
    hdr.kernel_addr =  0x10008000;
    hdr.ramdisk_addr = 0x11000000;
    hdr.second_addr =  0x10F00000;
    hdr.tags_addr =    0x10000100;

    hdr.page_size = pagesize;

    while(argc > 0){
        char *arg = argv[0];
        char *val = argv[1];
        if(argc < 2) {
            return usage();
        }
        argc -= 2;
        argv += 2;
        if(!strcmp(arg, "--output") || !strcmp(arg, "-o")) {
            bootimg = val;
        } else if(!strcmp(arg, "--kernel")) {
            kernel_fn = val;
        } else if(!strcmp(arg, "--ramdisk")) {
            ramdisk_fn = val;
        } else if(!strcmp(arg, "--second")) {
            second_fn = val;
        } else if(!strcmp(arg, "--cmdline")) {
            cmdline = val;
        } else if(!strcmp(arg, "--base")) {
            unsigned base = strtoul(val, 0, 16);
            hdr.kernel_addr =  base + 0x00008000;
            hdr.ramdisk_addr = base + 0x01000000;
            hdr.second_addr =  base + 0x00F00000;
            hdr.tags_addr =    base + 0x00000100;
        } else if(!strcmp(arg, "--ramdiskaddr")) {
                fprintf(stderr,"ramdisk addr input ignored on MT65xx");
        } else if(!strcmp(arg, "--board")) {
            board = val;
        } else if(!strcmp(arg,"--pagesize")) {
                fprintf(stderr,"page size input ignored on MT65xx");
        } else if(!strcmp(arg,"--ot")) {
            out_type = val;
            if((!strcmp(out_type,"boot")) && (!strcmp(out_type,"recovery"))) {
                fprintf(stderr,"output type must be boot or recovery");
                return usage();
            }
        } else {
            return usage();
        }
    }

    if(bootimg == 0) {
        fprintf(stderr,"error: no output filename specified\n");
        return usage();
    }

    if(kernel_fn == 0) {
        fprintf(stderr,"error: no kernel image specified\n");
        return usage();
    }

    if(ramdisk_fn == 0) {
        fprintf(stderr,"error: no ramdisk image specified\n");
        return usage();
    }

    if(strlen(board) >= BOOT_NAME_SIZE) {
        fprintf(stderr,"error: board name too large\n");
        return usage();
    }

    strcpy((char *)hdr.name, board);

    memcpy(hdr.magic, BOOT_MAGIC, BOOT_MAGIC_SIZE);

    if(strlen(cmdline) > (BOOT_ARGS_SIZE - 1)) {
        fprintf(stderr,"error: kernel commandline too large\n");
        return 1;
    }
    strcpy((char*)hdr.cmdline, cmdline);

    kernel_data = load_file(kernel_fn, &hdr.kernel_size);
    if(kernel_data == 0) {
        fprintf(stderr,"error: could not load kernel '%s'\n", kernel_fn);
        return 1;
    }

    if(!strcmp(ramdisk_fn,"NONE")) {
        ramdisk_data = 0;
        hdr.ramdisk_size = 0;
    } else {
        ramdisk_data = load_file(ramdisk_fn, &hdr.ramdisk_size);
        if(ramdisk_data == 0) {
            fprintf(stderr,"error: could not load ramdisk '%s'\n", ramdisk_fn);
            return 1;
        }
    }

    if(second_fn) {
        second_data = load_file(second_fn, &hdr.second_size);
        if(second_data == 0) {
            fprintf(stderr,"error: could not load secondstage '%s'\n", second_fn);
            return 1;
        }
    }

    kernel_hdr.kernel_size=hdr.kernel_size;
    if(!strcmp(out_type,"boot")) {
        rootfs_hdr.rootfs_size=hdr.ramdisk_size;
    } else {
        recovery_hdr.recovery_size=hdr.ramdisk_size;
    }
    
    hdr.kernel_size=hdr.kernel_size+512;
    hdr.ramdisk_size=hdr.ramdisk_size+512;

    kernel_mt = strcat(kernel_fn,"-mt");
    fd1 = open(kernel_mt, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if(fd1 < 0) {
        fprintf(stderr,"error: could not create '%s'\n", kernel_mt);
        return 1;
    }
    if(write(fd1, &kernel_hdr, 512) != 512) goto fail;
    if(write(fd1, kernel_data, kernel_hdr.kernel_size) != (signed)kernel_hdr.kernel_size) goto fail;
    close(fd1);
    kernel_data = load_file(kernel_mt, &hdr.kernel_size);
    if(kernel_data == 0) {
        fprintf(stderr,"error: could not load kernel '%s'\n", kernel_mt);
        return 1;
    }

    ramdisk_mt = strcat(ramdisk_fn,"-mt");
    fd1 = open(ramdisk_mt, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if(fd1 < 0) {
        fprintf(stderr,"error: could not create '%s'\n", ramdisk_mt);
        return 1;
    }

    if(!strcmp(out_type,"boot")) {
        if(write(fd1, &rootfs_hdr, 512) != 512) goto fail;
        if(write(fd1, ramdisk_data, rootfs_hdr.rootfs_size) != (signed)rootfs_hdr.rootfs_size) goto fail;
    } else {
        if(write(fd1, &recovery_hdr, 512) != 512) goto fail;
        if(write(fd1, ramdisk_data, recovery_hdr.recovery_size) != (signed)recovery_hdr.recovery_size) goto fail;
    }
    close(fd1);

    if(!strcmp(ramdisk_mt,"NONE-mt")) {
        ramdisk_data = 0;
        hdr.ramdisk_size = 0;
    } else {
        ramdisk_data = load_file(ramdisk_fn, &hdr.ramdisk_size);
        if(ramdisk_data == 0) {
            fprintf(stderr,"error: could not load ramdisk '%s'\n", ramdisk_mt);
            return 1;
        }
    }

    /* put a hash of the contents in the header so boot images can be
     * differentiated based on their first 2k.
     */
    SHA_init(&ctx);
    SHA_update(&ctx, kernel_data, (int)hdr.kernel_size);
    SHA_update(&ctx, &hdr.kernel_size, (int)sizeof(hdr.kernel_size));
    SHA_update(&ctx, ramdisk_data, hdr.ramdisk_size);
    SHA_update(&ctx, &hdr.ramdisk_size, (int)sizeof(hdr.ramdisk_size));
    SHA_update(&ctx, second_data, (int)hdr.second_size);
    SHA_update(&ctx, &hdr.second_size, (int)sizeof(hdr.second_size));
    sha = SHA_final(&ctx);
    memcpy(hdr.id, sha,
           SHA_DIGEST_SIZE > sizeof(hdr.id) ? sizeof(hdr.id) : SHA_DIGEST_SIZE);

    fd = open(bootimg, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if(fd < 0) {
        fprintf(stderr,"error: could not create '%s'\n", bootimg);
        return 1;
    }

    if(write(fd, &hdr, sizeof(hdr)) != sizeof(hdr)) goto fail;
    if(write_padding(fd, pagesize, sizeof(hdr))) goto fail;

    if(write(fd, kernel_data, hdr.kernel_size) != (signed)hdr.kernel_size) goto fail;
    if(write_padding(fd, pagesize, hdr.kernel_size)) goto fail;

    if(write(fd, ramdisk_data, hdr.ramdisk_size) != (signed)hdr.ramdisk_size) goto fail;
    if(write_padding(fd, pagesize, hdr.ramdisk_size)) goto fail;

    if(second_data) {
        if(write(fd, second_data, hdr.second_size) != (signed)hdr.second_size) goto fail;
        if(write_padding(fd, pagesize, hdr.ramdisk_size)) goto fail;
    }

    return 0;

fail:
    unlink(bootimg);
    close(fd);
    fprintf(stderr,"error: failed writing '%s': %s\n", bootimg,
            strerror(errno));
    return 1;
}
