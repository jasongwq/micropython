#include "libmp.h"
#include "usart.h"
pyb_usart_t pyb_usart_global_debug = PYB_USART_NONE;
extern void __fatal_error(const char *msg);

void nlr_jump_fail(void *val) {
    __fatal_error("FATAL: uncaught exception");
}
void libmp_do_repl(void)
{
    pyexec_repl();
}

bool libmp_do_file(const char *filename)
{
    return pyexec_file(filename);
}

int libmp_init()
{
    // Micro Python init
    pendsv_init();
    gc_init(&_heap_start, &_heap_end);
    qstr_init();
    mp_init();

    //sdcard_init();
    storage_init();
    return 0;
}
