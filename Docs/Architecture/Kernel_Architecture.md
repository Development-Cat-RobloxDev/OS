# ImplusOS Kernel Architecture

## Scope
This document summarizes the current kernel structure for process execution, memory layout, and interrupt/syscall flow.

## Process Model
- Process state is managed by `Kernel/ProcessManager/ProcessManager_Create.c`.
- A process slot owns:
  - register save frame (`saved_rsp`, `saved_user_rsp`)
  - per-process page table (`cr3`)
  - kernel stack and user memory ranges
  - capability mask (`PROCESS_CAP_*`)
- Scheduling is cooperative with explicit yield points (`process_yield`) and syscall exit scheduling.
- Syscall entry/exit context frame format is defined in `Kernel/Syscall/Syscall_Main.h`.

## Memory Model
- Virtual ranges are defined in `Kernel/ProcessManager/ProcessManager.h`:
  - user code: `USER_CODE_BASE` .. `USER_CODE_LIMIT`
  - user heap: `USER_HEAP_BASE` .. `USER_HEAP_LIMIT`
  - user stack: `USER_STACK_BASE` .. `USER_STACK_TOP`
- Guard pages are installed for heap/stack boundaries.
- User buffer validation is enforced in syscall dispatch through:
  - `process_user_buffer_is_valid`
  - `process_user_cstring_length`

## Interrupt and Syscall Flow
- IDT setup: `Kernel/IDT/*`
- Syscall entry stubs: `Kernel/Syscall/Syscall_Entry.asm`
- Syscall dispatch core: `Kernel/Syscall/Syscall_Dispatch.c`
- File syscall backend: `Kernel/Syscall/Syscall_File.c`
- Input polling is integrated in syscall dispatch (`ps2_input_poll`) to keep event queues refreshed.

## Status and Error Policy
- Kernel returns signed `os_status_t` values (`Kernel/Common/Status.h`).
- `0` or positive values indicate success / non-error payloads.
- Negative values indicate errors and map to userland `os_errno`.
- Canonical mapping table: `Docs/Architecture/Status_Codes.md`.

## Window Manager Architecture
- Window manager core: `Kernel/WindowManager/*`
- **Modern Design Features**:
  - Gradient-based background rendering for visual depth
  - Smooth window frame decorations with active/inactive states
  - Enhanced titlebar with icons for minimize, maximize, and close buttons
  - Soft shadow effects for window depth perception
  - Color-coded window states and hover effects
  - Font rendering support via TrueType through `stb_truetype`
- Per-process window slots with process ownership validation
- Window drawing operations are per-process syscall-driven
- Window redraw is coordinated through `window_manager_present_for_process()`
- Syscall backend: `Kernel/Syscall/*`

## Desktop Subsystem
- Desktop component: `Kernel/Desktop/*`
- **Features**:
  - Desktop background rendering with gradient color schemes (top to bottom)
  - Desktop icon management system (max 128 icons per desktop)
  - Icon rendering with labeled captions
  - Click event handling for desktop and window interactions
  - Icon lifecycle management (add, remove, query)
- Thread-safe icon operations via spinlock protection
- Desktop initialization occurs after window manager setup
- Integration with window manager for coordinated rendering

## Display Driver Architecture
- Display driver interface: `Kernel/Drivers/Display/Display_Driver.h`
- **Double-Buffering Support (Generic Framebuffer)**:
  - Off-screen back-buffer allocation for flicker-free rendering
  - Enable: `fb_enable_double_buffering()`
  - Disable: `fb_disable_double_buffering()`
  - Present: `fb_present()` swaps back-buffer to framebuffer
  - Clear: `fb_clear()` fills with specified color
  - Automatic back-buffer cleanup on framebuffer reconfiguration
- Generic framebuffer: `Kernel/Drivers/Display/ImplusOS_Generic/*`
  - Chunked memory mapping for large framebuffers (2MB granules)
  - Supports both direct and double-buffered rendering modes
- VirtIO GPU driver: `Kernel/Drivers/Display/VirtIO/*`
