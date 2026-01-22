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
#define CF_MAX_CONFIGS 64

#define CF_ERR_LOG(...) fprintf(stderr, __VA_ARGS__)
#define CF_WRN_LOG(...) fprintf(stdout, __VA_ARGS__)

#define CF_SUCCESS_EC 0
#define CF_MAX_TARGETS_EC 1
#define CF_TARGET_NOT_FOUND_EC 2
#define CF_MAX_CONFIGS_EC 3
#define CF_INVALID_STATE_EC 4

typedef uint32_t (*cf_target_fn)(uint32_t argc, char** argv);
typedef void (*cf_config_fn)(void);

typedef struct {
    const char* name;
    cf_target_fn fn;
    bool executed;
} cf_target_decl_t;

typedef struct {
    const char* name;
    cf_config_fn fn;
} cf_config_decl_t;

typedef enum {
    REGISTER_PHASE = 0,
    CONFIG_CONSTRUCT_PHASE = 1,
    TARGET_CONSTRUCT_PHASE = 2
} cf_state_t;

static cf_target_decl_t cf_targets[CF_MAX_TARGETS] = { 0 };
static size_t cf_num_targets = 0;

static cf_config_decl_t cf_configs[CF_MAX_CONFIGS] = { 0 };
static size_t cf_num_configs = 0;

static cf_state_t cf_state = REGISTER_PHASE;

static void cf_register_target(const char* name, cf_target_fn fn) {
    if (cf_state != REGISTER_PHASE) {
        CF_ERR_LOG("Error: Invalid cf_state (%d) when registering target!\n", cf_state);
        exit(CF_INVALID_STATE_EC);
    }

    if (cf_num_targets >= CF_MAX_TARGETS) {
        CF_ERR_LOG("Error: Maximum targets of %d was reached!\n", CF_MAX_TARGETS);
        exit(CF_MAX_TARGETS_EC);
    }

    cf_targets[cf_num_targets++] = (cf_target_decl_t) {
        .name = name,
        .fn = fn,
        .executed = false
    };
}

static void cf_register_config(const char* name, cf_config_fn fn) {
    if (cf_state != REGISTER_PHASE) {
        CF_ERR_LOG("Error: Invalid cf_state (%d) when registering config!\n", cf_state);
        exit(CF_INVALID_STATE_EC);
    }

    if (cf_num_targets >= CF_MAX_TARGETS) {
        CF_ERR_LOG("Error: Maximum configs of %d was reached!\n", CF_MAX_CONFIGS);
        exit(CF_MAX_CONFIGS_EC);
    }

    cf_configs[cf_num_configs++] = (cf_config_decl_t) {
        .name = name,
        .fn = fn
    };
}

#define CF_TARGET(name_ident) \
    static uint32_t cf_target_##name_ident(uint32_t argc, char** argv); \
    __attribute__((constructor)) static void cf_target_reg_##name_ident(void) { \
        cf_register_target(#name_ident, cf_target_##name_ident); \
    } \
    static uint32_t cf_target_##name_ident(uint32_t argc, char** argv)


#define CF_CONFIG(name_ident) \
    static void cf_config_##name_ident(void); \
    __attribute__((constructor)) static void cf_config_reg_##name_ident(void) { \
        cf_register_config(#name_ident, cf_config_##name_ident); \
    } \
    static void cf_config_##name_ident(void)


#define CF_FINISH(retval) \
    (void) argc; \
    (void) argv; \
    return retval;

__attribute__((weak)) int main(int argc, char** argv) {
    (void) cf_register_config;
    (void) cf_register_target;

    if (argc == 1) {
        return CF_SUCCESS_EC;
    }

    cf_state = CONFIG_CONSTRUCT_PHASE;
    for (size_t i = 0; i < cf_num_configs; i++) {
        cf_configs[i].fn();
    }

    cf_state = TARGET_CONSTRUCT_PHASE;
    for (int32_t i = 1; i < argc; i++) {
        for (size_t j = 0; j < cf_num_targets; j++) {
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
