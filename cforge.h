#ifndef CFORGE_H
#define CFORGE_H

#if 0
cc -O2 -Wall -Wextra "cforge.c" -o "./.b" && exec "./.b" "$@"
exit 0
#endif

#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CF_MAX_TARGETS 64

#define CF_ERR_LOG(...) fprintf(stderr, __VA_ARGS__)
#define CF_WRN_LOG(...) fprintf(stdout, __VA_ARGS__)

#define CF_SUCCESS_EC 0
#define CF_MAX_TARGETS_EC 1
#define CF_TARGET_NOT_FOUND_EC 2

typedef uint32_t (*cf_target_fn)(uint32_t argc, char** argv);

typedef struct {
    const char* name;
    cf_target_fn fn;
    bool executed;
} cf_target_decl_t;

static cf_target_decl_t cf_targets[CF_MAX_TARGETS] = { 0 };
static size_t cf_num_targets = 0;

static void cf_register_target(const char* name, cf_target_fn fn) {
    if (cf_num_targets >= CF_MAX_TARGETS) {
        CF_ERR_LOG("Error: Maximum targets of %d was reached!\n", CF_MAX_TARGETS);
        exit(CF_TARGET_NOT_FOUND_EC);
    }
    cf_targets[cf_num_targets++] = (cf_target_decl_t) {
        .name = name,
        .fn = fn,
        .executed = false
    };
}

#define CF_TARGET(name_ident) \
    static uint32_t cf_target_##name_ident(uint32_t argc, char** argv); \
    __attribute__((constructor)) static void cf_target_reg_##name_ident(void) { \
        cf_register_target(#name_ident, cf_target_##name_ident); \
    } \
    static uint32_t cf_target_##name_ident(uint32_t argc, char** argv)

#define CF_FINISH(retval) \
    (void) argc; \
    (void) argv; \
    return retval;

__attribute__((weak)) int main(int argc, char** argv) {
    if (argc == 1) {
        return CF_SUCCESS_EC;
    }

    for (int32_t i = 1; i < argc; i++) {
        for (uint32_t j = 0; j < cf_num_targets; j++) {
            cf_target_decl_t* target = &cf_targets[j];
            if (strcmp(target->name, argv[i]) == 0) {
                if (target->executed) {
                    CF_WRN_LOG("Warning: Target \"%s\" was executed already! Skipping target...\n", argv[i]);
                    goto next_iter;
                }
                target->executed = true;
                target->fn(0, NULL);
                goto next_iter;
            }
        }

        CF_ERR_LOG("Error: Target \"%s\" not found!\n", argv[i]);
        return CF_TARGET_NOT_FOUND_EC;

        next_iter:
            continue;
    }

    return CF_SUCCESS_EC;
}

#endif // CFORGE_H
