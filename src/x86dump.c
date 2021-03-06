/*
 * VB6Tracer - VB6 Instrumentation
 * Copyright (C) 2013-2014 Jurriaan Bremer, Marion Marschalek
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdint.h>
#include <windows.h>
#include "vb6.h"
#include "distorm.h"
#include "mnemonics.h"

static const uint8_t *_module_from_address(const uint8_t *addr)
{
    addr = (const uint8_t *)((uintptr_t) addr & ~0xfff);
    while (*addr != 'M' || addr[1] != 'Z') {
        addr -= 0x1000;
    }
    return addr;
}

const char *symbol(const uint8_t *addr)
{
    const uint8_t *mod; static char name[MAX_PATH+32];

    mod = _module_from_address(addr);
    if(mod == NULL) return NULL;

    int len = GetModuleFileName((void *) mod, name, sizeof(name));

    IMAGE_DOS_HEADER *image_dos_header = (IMAGE_DOS_HEADER *) mod;
    IMAGE_NT_HEADERS *image_nt_headers =
        (IMAGE_NT_HEADERS *)(mod + image_dos_header->e_lfanew);

    IMAGE_DATA_DIRECTORY *data_directories =
        image_nt_headers->OptionalHeader.DataDirectory;

    IMAGE_DATA_DIRECTORY *export_data_directory =
        &data_directories[IMAGE_DIRECTORY_ENTRY_EXPORT];

    IMAGE_EXPORT_DIRECTORY *export_directory = (IMAGE_EXPORT_DIRECTORY *)(
        mod + export_data_directory->VirtualAddress);

    uint32_t *function_addresses = (uint32_t *)(
        mod + export_directory->AddressOfFunctions);

    uint32_t *names_addresses = (uint32_t *)(
        mod + export_directory->AddressOfNames);

    uint16_t *ordinals = (uint16_t *)(
        mod + export_directory->AddressOfNameOrdinals);

    for (uint32_t i = 0; i < export_directory->NumberOfNames; i++) {
        if(mod + function_addresses[ordinals[i]] == addr) {
            name[len++] = '!';
            strncpy(&name[len], (const char *) mod + names_addresses[i],
                sizeof(name) - len);

            const char *ptr = strrchr(name, '\\');
            if(ptr == NULL) ptr = strrchr(name, '/');
            return ptr == NULL ? name : ptr+1;
        }
    }
    return NULL;
}

void x86dump(const uint8_t *addr, const char *msg)
{
    report("[x] Disassembling 0x%x: %z", addr, msg);

    while (1) {
        unsigned int used_instruction_count = 0; _DecodedInst ins;

        int ret = distorm_decode((uintptr_t) addr, addr, 16, Decode32Bits,
                &ins, 1, &used_instruction_count);
        if((ret != DECRES_SUCCESS && ret != DECRES_MEMORYERR) ||
                used_instruction_count != 1) {
            report("[-] Disassembly failed");
            return;
        }

        char extra[256];
        extra[0] = 0;

        if(*addr == 0xe8) {
            const uint8_t *fn = addr + *(const uint32_t *)(addr + 1) + 5;
            sprintf(extra, "; 0x%p %s", fn, symbol(fn));
        }
        else if(*addr == 0xff && addr[1] == 0x25) {
            const uint8_t *fn = **(const uint8_t ***)(addr + 2);
            sprintf(extra, "; 0x%p %s", fn, symbol(fn));
        }
        // b879e0817c   MOV EAX, 0x7c81e079
        // ffd0         CALL EAX
        else if(*addr == 0xb8 && addr[5] == 0xff && addr[6] == 0xd0) {
            const uint8_t *fn = *(const uint8_t **)(addr + 1);
            sprintf(extra, "; 0x%p %s", fn, symbol(fn));
        }

        char hex[33];
        sprintf(hex, "%-12s", ins.instructionHex.p);
        if(extra[0] != 0) {
            report("0x%x %z %z %z %z", addr, hex,
                ins.mnemonic.p, ins.operands.p, extra);
        }
        else {
            report("0x%x %z %z %z", addr, hex,
                ins.mnemonic.p, ins.operands.p);
        }

        switch (*addr) {
        case 0xc2: case 0xc3:
            return;

        case 0xff:
            switch (addr[1]) {
            case 0x25: case 0xe0: case 0xe1:
                return;
            }
        }

        addr += ins.size;
    }
}
