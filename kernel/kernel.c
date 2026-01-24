void kmain(void) {
    const char* msg = "Hello from C Kernel!";
    char* video = (char*)0xB8000;

    for (int i = 0; msg[i]; i++) {
        video[i * 2] = msg[i];
        video[i * 2 + 1] = 0x07; // 白文字
    }

    while (1) {}
}
