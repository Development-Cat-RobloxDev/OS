// kernel.c

typedef unsigned char uint8_t;

#define VIDEO_MEMORY    (0xB8000)   // VGAメモリの開始アドレス
#define WHITE_ON_BLACK  (0x07)      // 白文字/黒背景の属性

// 画面クリア関数
void clear_screen(void)
{
    uint8_t* video = (uint8_t*)VIDEO_MEMORY;    // VGAメモリポインタ
    uint8_t attr = WHITE_ON_BLACK;  // 属性

    for (int i = 0; i < 80 * 25; i++)
    {
        *video++ = ' ';     // 空白文字
        *video++ = attr;    // 属性を書き込み
    }
}

// 文字列表示関数
void print_string(const char* str)
{
    uint8_t* video = (uint8_t*)VIDEO_MEMORY;    // VGAメモリポインタ
    uint8_t attr = WHITE_ON_BLACK;  // 属性

    while (*str)
    {
        *video++ = *str++;  // 文字を書き込み
        *video++ = attr;    // 属性を書き込み
    }
}

// カーネルメイン関数
void kernel_main(void)
{
    clear_screen();                     // 画面をクリア
    print_string("Kernel started!");    // 文字列を表示

    while (1)                           // 無限ループ
    {
        __asm__ volatile ("cli; hlt");  // 停止
    }
}