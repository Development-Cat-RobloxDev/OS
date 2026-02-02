#include "Kernel_Main.h"
#include "Memory/Memory_Main.h"
#include "Paging/Paging_Main.h"
#include "FrameBuffer/FrameBuffer_Main.h"
#include "Font/Font_Main.h"
#include "IDT/IDT_Main.h"
#include "IO/IO_Main.h"
#include "WindowManager/WindowManager_Main.h"
#include "Mouse/Mouse_Main.h"
#include "Drivers/FileSystem/FAT32/FAT32_Main.h"

#define COM1_PORT 0x3F8

void serial_init(void) {
    outb(COM1_PORT + 1, 0x00);    // Disable interrupts
    outb(COM1_PORT + 3, 0x80);    // Enable DLAB
    outb(COM1_PORT + 0, 0x03);    // Divisor low  (38400 baud)
    outb(COM1_PORT + 1, 0x00);    // Divisor high
    outb(COM1_PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(COM1_PORT + 2, 0xC7);    // Enable FIFO
    outb(COM1_PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

void serial_write_char(char c) {
    while ((inb(COM1_PORT + 5) & 0x20) == 0); // Wait for TX empty
    outb(COM1_PORT, c);
}

void serial_write_string(const char* str) {
    while (*str) {
        if (*str == '\n')
            serial_write_char('\r');
        serial_write_char(*str++);
    }
}

void serial_write_uint64(uint64_t value) {
    char hex[] = "0123456789ABCDEF";
    serial_write_string("0x");

    for (int i = 60; i >= 0; i -= 4) {
        serial_write_char(hex[(value >> i) & 0xF]);
    }
}

void serial_write_uint32(uint32_t value) {
    serial_write_uint64((uint64_t)value);
}

void serial_write_uint16(uint16_t value) {
    serial_write_uint64((uint64_t)value);
}

__attribute__((noreturn))
void kernel_main(BOOT_INFO *boot_info){
    fat32_list_root_files();
    // --- メモリ初期化 ---
    init_physical_memory(
        boot_info->MemoryMap,
        boot_info->MemoryMapSize,
        boot_info->MemoryMapDescriptorSize
    );
    
    // ヒープ初期化（kmalloc/kfreeが使えるようになる）
    memory_init();

    // --- フレームバッファ初期化 ---
    fb_init((void*)boot_info->FrameBufferBase,
            boot_info->HorizontalResolution,
            boot_info->VerticalResolution,
            boot_info->PixelsPerScanLine);

    // 画面クリア
    fb_clear(0x2C3E50); // ダークブルーグレー

    // --- ページング初期化 ---
    init_paging();

    // --- 割り込み初期化 ---
    init_idt();

    // --- ウィンドウマネージャ初期化 ---
    window_init();

    // --- TTFフォント初期化 ---
    // FAT32が必要なので後で初期化する
    
    // --- FAT32初期化 ---
    int fat32_ok = 0;
    if(fat32_init()){
        fat32_ok = 1;
        
        // TTFフォントを読み込み（NotoSansJP.ttfがある場合）
        if(init_ttf_font()){
            // TTFフォント初期化成功
        }
    }

    // --- メインウィンドウ作成 ---
    WINDOW *main_win = window_create_with_title(50, 50, 600, 400, 0xF0F0F0, "システム情報");
    
    // --- サブウィンドウ作成（例） ---
    WINDOW *logo_win = window_create_with_title(700, 100, 250, 250, 0xFFFFFF, "ロゴ");

    // --- 情報表示 ---
    int y_pos = 10;
    int line_height = 20;
    
    // システム情報を表示
    window_draw_text(main_win, 10, y_pos, "カーネル起動完了", 0x000000);
    y_pos += line_height;
    
    // 解像度情報
    char res_text[64];
    // 簡易的な数値→文字列変換（sprintf代わり）
    window_draw_text(main_win, 10, y_pos, "解像度: ", 0x000000);
    // ここでは固定文字列として表示
    window_draw_text(main_win, 100, y_pos, "1920x1080", 0x000000); // 実際の値に応じて変更
    y_pos += line_height;
    
    // メモリ情報
    window_draw_text(main_win, 10, y_pos, "メモリ初期化: OK", 0x008000);
    y_pos += line_height;
    
    // ページング情報
    window_draw_text(main_win, 10, y_pos, "ページング: 有効", 0x008000);
    y_pos += line_height;
    
    // FAT32情報
    if(fat32_ok){
        window_draw_text(main_win, 10, y_pos, "FAT32: 初期化成功", 0x008000);
        y_pos += line_height;
        
        // ファイル検索
        FAT32_FILE file = {0};
        if(fat32_find_file("LOGO       PNG", &file)){
            window_draw_text(logo_win, 10, 10, "LOGO.PNG 検出", 0x008000);
            
            // ファイルサイズ表示（簡易版）
            window_draw_text(logo_win, 10, 30, "ファイルサイズ:", 0x000000);
            // 実際のサイズを表示する場合は数値変換が必要
            
            // 画像を読み込んで表示（オプション）
            // uint32_t filesize = fat32_get_file_size(&file);
            // uint8_t *buffer = (uint8_t*)kmalloc(filesize);
            // if(buffer && fat32_read_file(&file, buffer)){
            //     // PNGデコードして表示（要PNGデコーダ実装）
            //     kfree(buffer);
            // }
        } else {
            window_draw_text(logo_win, 10, 10, "LOGO.PNG 未検出", 0xFF0000);
        }
        
        // テストファイル検索
        if(fat32_find_file("NOTOSANSJP   TTF", &file)){
            window_draw_text(main_win, 10, y_pos, "フォント: 検出", 0x008000);
        } else {
            window_draw_text(main_win, 10, y_pos, "フォント: 未検出", 0xFF8800);
        }
        y_pos += line_height;
        
    } else {
        window_draw_text(main_win, 10, y_pos, "FAT32: 初期化失敗", 0xFF0000);
        y_pos += line_height;
    }
    
    // マウス情報
    window_draw_text(main_win, 10, y_pos, "マウス: 初期化中...", 0x000080);
    y_pos += line_height;

    // --- デモウィンドウ追加 ---
    WINDOW *demo_win = window_create_with_title(100, 300, 400, 200, 0xE8F4F8, "デモウィンドウ");
    window_draw_text(demo_win, 10, 10, "TTFフォントテスト", 0x000000);
    window_draw_text(demo_win, 10, 35, "日本語表示テスト", 0x0000FF);
    window_draw_text(demo_win, 10, 60, "マウスでドラッグ可能", 0x008000);
    
    // 複数行テキストのデモ
    window_draw_text_multiline(demo_win, 10, 90, 
        "行1: システム情報\n行2: メモリ管理\n行3: ウィンドウシステム", 
        0x000000, 20);

    // --- すべてのウィンドウを描画 ---
    window_draw_all();

    // --- マウス初期化 ---
    mouse_init();

    // --- メインループ ---
    // 実際にはマウスイベントやキーボードイベントを処理
    while(1) {
        // 画面を再描画（マウス移動などで変更があった場合）
        // window_draw_all(); // 必要に応じてコメント解除
        
        __asm__("hlt"); // 割り込み待機
    }
}

// 補助関数: 整数を文字列に変換（簡易版）
static void int_to_str(int value, char *buffer, int buffer_size) {
    if (buffer_size <= 0) return;
    
    int is_negative = 0;
    if (value < 0) {
        is_negative = 1;
        value = -value;
    }
    
    int pos = 0;
    
    // 数値を逆順に格納
    do {
        if (pos >= buffer_size - 1) break;
        buffer[pos++] = '0' + (value % 10);
        value /= 10;
    } while (value > 0);
    
    // 負号追加
    if (is_negative && pos < buffer_size - 1) {
        buffer[pos++] = '-';
    }
    
    buffer[pos] = '\0';
    
    // 反転
    for (int i = 0; i < pos / 2; i++) {
        char temp = buffer[i];
        buffer[i] = buffer[pos - 1 - i];
        buffer[pos - 1 - i] = temp;
    }
}

// 使用例（メインの中で使う場合）:
// char res_buffer[32];
// int_to_str(boot_info->HorizontalResolution, res_buffer, sizeof(res_buffer));
// window_draw_text(main_win, 100, y_pos, res_buffer, 0x000000);