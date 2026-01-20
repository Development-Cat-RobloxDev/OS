typedef unsigned char  u8;
typedef unsigned short u16;

volatile u16* vga = (u16*)0xB8000;
int cursor = 0;

void putc(char c)
{
    vga[cursor++] = (0x0F << 8) | c;
}

void puts(const char* s)
{
    while (*s) putc(*s++);
}

void kernel_main(void)
{
    puts("Hello 32-bit kernel!");
    for (;;);
}
