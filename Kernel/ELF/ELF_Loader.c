#include "ELF_Loader.h"

#include "../Drivers/FileSystem/FAT32/FAT32_Main.h"
#include "../Memory/Memory_Main.h"
#include "../Memory/Other_Utils.h"
#include "../Serial.h"

#include <stddef.h>
#include <stdint.h>

#define ELF_MAGIC_0 0x7F
#define ELF_MAGIC_1 'E'
#define ELF_MAGIC_2 'L'
#define ELF_MAGIC_3 'F'
#define PT_LOAD 1

typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

static char to_upper_ascii(char c)
{
    if (c >= 'a' && c <= 'z') {
        return (char)(c - ('a' - 'A'));
    }
    return c;
}

bool elf_loader_path_to_fat83(const char *path, char out_name[12])
{
    if (!path || !out_name) {
        return false;
    }

    for (int i = 0; i < 11; ++i) {
        out_name[i] = ' ';
    }
    out_name[11] = '\0';

    int name_len = 0;
    int ext_len = 0;
    int in_ext = 0;

    for (const char *p = path; *p; ++p) {
        char c = *p;

        if (c == '/') {
            name_len = 0;
            ext_len = 0;
            in_ext = 0;
            for (int i = 0; i < 11; ++i) {
                out_name[i] = ' ';
            }
            continue;
        }

        if (c == '.') {
            if (in_ext) {
                return false;
            }
            in_ext = 1;
            continue;
        }

        c = to_upper_ascii(c);

        if (!in_ext) {
            if (name_len >= 8) {
                return false;
            }
            out_name[name_len++] = c;
        } else {
            if (ext_len >= 3) {
                return false;
            }
            out_name[8 + ext_len++] = c;
        }
    }

    return name_len > 0;
}

static bool in_vaddr_range(uint64_t start,
                           uint64_t size,
                           uint64_t min_vaddr,
                           uint64_t max_vaddr)
{
    if (size == 0) {
        return false;
    }
    if (start < min_vaddr) {
        return false;
    }
    if (start >= max_vaddr) {
        return false;
    }
    if (size > (max_vaddr - start)) {
        return false;
    }
    return true;
}

bool elf_loader_load_from_fat83(const char fat_name[12],
                                const elf_load_policy_t *policy,
                                uint64_t *entry_out)
{
    if (!fat_name || !policy || !entry_out) {
        return false;
    }

    FAT32_FILE file;
    if (!fat32_find_file(fat_name, &file)) {
        serial_write_string("[OS] [ELF] file not found\n");
        return false;
    }

    if (file.size == 0 || (uint64_t)file.size > policy->max_file_size) {
        serial_write_string("[OS] [ELF] invalid size\n");
        return false;
    }

    uint8_t *buffer = (uint8_t *)kmalloc(file.size);
    if (!buffer) {
        serial_write_string("[OS] [ELF] out of memory\n");
        return false;
    }

    if (!fat32_read_file(&file, buffer)) {
        serial_write_string("[OS] [ELF] read failed\n");
        kfree(buffer);
        return false;
    }

    if ((uint64_t)file.size < sizeof(Elf64_Ehdr)) {
        serial_write_string("[OS] [ELF] header too small\n");
        kfree(buffer);
        return false;
    }

    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)buffer;
    if (ehdr->e_ident[0] != ELF_MAGIC_0 ||
        ehdr->e_ident[1] != ELF_MAGIC_1 ||
        ehdr->e_ident[2] != ELF_MAGIC_2 ||
        ehdr->e_ident[3] != ELF_MAGIC_3) {
        serial_write_string("[OS] [ELF] invalid magic\n");
        kfree(buffer);
        return false;
    }

    if (ehdr->e_phentsize != sizeof(Elf64_Phdr) || ehdr->e_phnum == 0) {
        serial_write_string("[OS] [ELF] invalid phdr info\n");
        kfree(buffer);
        return false;
    }

    uint64_t phdr_table_bytes = (uint64_t)ehdr->e_phnum * (uint64_t)ehdr->e_phentsize;
    if (ehdr->e_phoff > file.size || phdr_table_bytes > ((uint64_t)file.size - ehdr->e_phoff)) {
        serial_write_string("[OS] [ELF] phdr out of range\n");
        kfree(buffer);
        return false;
    }

    Elf64_Phdr *phdrs = (Elf64_Phdr *)(buffer + ehdr->e_phoff);
    int load_segments = 0;

    for (uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        Elf64_Phdr *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD) {
            continue;
        }

        if (ph->p_memsz < ph->p_filesz) {
            serial_write_string("[OS] [ELF] memsz < filesz\n");
            kfree(buffer);
            return false;
        }

        if (!in_vaddr_range(ph->p_vaddr, ph->p_memsz, policy->min_vaddr, policy->max_vaddr)) {
            serial_write_string("[OS] [ELF] vaddr out of range\n");
            kfree(buffer);
            return false;
        }

        if (ph->p_offset > file.size || ph->p_filesz > ((uint64_t)file.size - ph->p_offset)) {
            serial_write_string("[OS] [ELF] segment out of file range\n");
            kfree(buffer);
            return false;
        }

        uint8_t *dst = (uint8_t *)(uintptr_t)ph->p_vaddr;
        uint8_t *src = buffer + ph->p_offset;

        memcpy(dst, src, (size_t)ph->p_filesz);
        for (uint64_t j = ph->p_filesz; j < ph->p_memsz; ++j) {
            dst[j] = 0;
        }

        ++load_segments;
    }

    if (load_segments == 0) {
        serial_write_string("[OS] [ELF] no load segment\n");
        kfree(buffer);
        return false;
    }

    if (ehdr->e_entry < policy->min_vaddr || ehdr->e_entry >= policy->max_vaddr) {
        serial_write_string("[OS] [ELF] entry out of range\n");
        kfree(buffer);
        return false;
    }

    *entry_out = ehdr->e_entry;
    kfree(buffer);
    return true;
}

bool elf_loader_load_from_path(const char *path,
                               const elf_load_policy_t *policy,
                               uint64_t *entry_out)
{
    if (!path || !policy || !entry_out) {
        return false;
    }

    return elf_loader_load_from_fat83(path, policy, entry_out);
}
