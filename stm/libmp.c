#include "libmp.h"
#define READLINE_HIST_SIZE (8)
extern float sqrtf(float x);
static const char *readline_hist[READLINE_HIST_SIZE] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

char *strdup(const char *str) {
    uint32_t len = strlen(str);
    char *s2 = m_new(char, len + 1);
    memcpy(s2, str, len);
    s2[len] = 0;
    return s2;
}

static void stdout_tx_str(const char *str) {
    usb_vcp_send_str(str);
}

static int readline(vstr_t *line, const char *prompt) {
    stdout_tx_str(prompt);
    int len = vstr_len(line);
    int escape = 0;
    int hist_num = 0;
    for (;;) {
        char c;
        for (;;) {
            if (usb_vcp_rx_any() != 0) {
                c = usb_vcp_rx_get();
                break;
            } 
            sys_tick_delay_ms(1);
            if (storage_needs_flush()) {
                storage_flush();
            }
        }
        if (escape == 0) {
            if (c == 4 && vstr_len(line) == len) {
                return 0;
            } else if (c == '\r') {
                stdout_tx_str("\r\n");
                for (int i = READLINE_HIST_SIZE - 1; i > 0; i--) {
                    readline_hist[i] = readline_hist[i - 1];
                }
                readline_hist[0] = strdup(vstr_str(line));
                return 1;
            } else if (c == 27) {
                escape = true;
            } else if (c == 127) {
                if (vstr_len(line) > len) {
                    vstr_cut_tail(line, 1);
                    stdout_tx_str("\b \b");
                }
            } else if (32 <= c && c <= 126) {
                vstr_add_char(line, c);
                stdout_tx_str(line->buf + line->len - 1);
            }
        } else if (escape == 1) {
            if (c == '[') {
                escape = 2;
            } else {
                escape = 0;
            }
        } else if (escape == 2) {
            escape = 0;
            if (c == 'A') {
                // up arrow
                if (hist_num < READLINE_HIST_SIZE && readline_hist[hist_num] != NULL) {
                    // erase line
                    for (int i = line->len - len; i > 0; i--) {
                        stdout_tx_str("\b \b");
                    }
                    // set line to history
                    line->len = len;
                    vstr_add_str(line, readline_hist[hist_num]);
                    // draw line
                    stdout_tx_str(readline_hist[hist_num]);
                    // increase hist num
                    hist_num += 1;
                }
            }
        } else {
            escape = 0;
        }
        sys_tick_delay_ms(10);
    }
}

void libmp_do_repl(void) {
    stdout_tx_str("Micro Python build <git hash> on 25/1/2014; " MICROPY_HW_BOARD_NAME " with STM32F405RG\r\n");
    stdout_tx_str("Type \"help()\" for more information.\r\n");

    vstr_t line;
    vstr_init(&line, 32);

    for (;;) {
        vstr_reset(&line);
        int ret = readline(&line, ">>> ");
        if (ret == 0) {
            // EOF
            break;
        }

        if (vstr_len(&line) == 0) {
            continue;
        }

        if (mp_repl_is_compound_stmt(vstr_str(&line))) {
            for (;;) {
                vstr_add_char(&line, '\n');
                int len = vstr_len(&line);
                int ret = readline(&line, "... ");
                if (ret == 0 || vstr_len(&line) == len) {
                    // done entering compound statement
                    break;
                }
            }
        }

        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, vstr_str(&line), vstr_len(&line), 0);
        qstr parse_exc_id;
        const char *parse_exc_msg;
        mp_parse_node_t pn = mp_parse(lex, MP_PARSE_SINGLE_INPUT, &parse_exc_id, &parse_exc_msg);
        qstr source_name = mp_lexer_source_name(lex);

        if (pn == MP_PARSE_NODE_NULL) {
            // parse error
            mp_lexer_show_error_pythonic_prefix(lex);
            printf("%s: %s\n", qstr_str(parse_exc_id), parse_exc_msg);
            mp_lexer_free(lex);
        } else {
            // parse okay
            mp_lexer_free(lex);
            mp_obj_t module_fun = mp_compile(pn, source_name, true);
            mp_parse_node_free(pn);
            if (module_fun != mp_const_none) {
                nlr_buf_t nlr;
                if (nlr_push(&nlr) == 0) {
                    rt_call_function_0(module_fun);
                    nlr_pop();
                } else {
                    // uncaught exception
                    mp_obj_print_exception((mp_obj_t)nlr.ret_val);
                }
            }
        }
    }

    stdout_tx_str("\r\n");
}

bool libmp_do_file(const char *filename) {
    mp_lexer_t *lex = mp_lexer_new_from_file(filename);

    if (lex == NULL) {
        printf("could not open file '%s' for reading\n", filename);
        return false;
    }

    qstr parse_exc_id;
    const char *parse_exc_msg;
    mp_parse_node_t pn = mp_parse(lex, MP_PARSE_FILE_INPUT, &parse_exc_id, &parse_exc_msg);
    qstr source_name = mp_lexer_source_name(lex);

    if (pn == MP_PARSE_NODE_NULL) {
        // parse error
        mp_lexer_show_error_pythonic_prefix(lex);
        printf("%s: %s\n", qstr_str(parse_exc_id), parse_exc_msg);
        mp_lexer_free(lex);
        return false;
    }

    mp_lexer_free(lex);

    mp_obj_t module_fun = mp_compile(pn, source_name, false);
    mp_parse_node_free(pn);

    if (module_fun == mp_const_none) {
        return false;
    }

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        rt_call_function_0(module_fun);
        nlr_pop();
        return true;
    } else {
        // uncaught exception
        mp_obj_print_exception((mp_obj_t)nlr.ret_val);
        return false;
    }
}

int libmp_init()
{
    // Micro Python init
    storage_init();
    gc_init(&_heap_start, &_heap_end);
    qstr_init();
    rt_init();
    return 0;
}

machine_float_t machine_sqrt(machine_float_t x) {
    // TODO
    return sqrtf(x);
}
