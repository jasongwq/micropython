#include "libmp.h"
#include "usart.h"
pyb_usart_t pyb_usart_global_debug = PYB_USART_NONE;

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
    //sdcard_init();
    storage_init();
    gc_init(&_heap_start, &_heap_end);
    qstr_init();
    rt_init();
    return 0;
}
double __aeabi_f2d(float x) {
    // TODO
    return 0.0;
}

float __aeabi_d2f(double x) {
    // TODO
    return 0.0;
}
machine_float_t machine_sqrt(machine_float_t x) {
    asm volatile (
            "vsqrt.f32  %[r], %[x]\n"
            : [r] "=t" (x)
            : [x] "t"  (x));
    return x;
}
