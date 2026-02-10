#ifndef CFORGE_H
#define CFORGE_H

#if 0
cc -O2 -Wall -Wextra "cforge.c" -o "./.b" && exec "./.b" "$@"
exit 0
#endif

#include <glob.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CF_MAX_TARGETS 64
#define CF_MAX_CONFIGS 64
#define CF_MAX_GLOBS 64

#define CF_MAX_NAME_LENGTH 127

#define CF_ERR_LOG(...) fprintf(stderr, __VA_ARGS__)
#define CF_WRN_LOG(...) fprintf(stdout, __VA_ARGS__)

#define CF_SUCCESS_EC 0
#define CF_MAX_TARGETS_EC 1
#define CF_TARGET_NOT_FOUND_EC 2
#define CF_MAX_CONFIGS_EC 3
#define CF_INVALID_STATE_EC 4
#define CF_NAME_TOO_LONG_EC 5
#define CF_CLIB_FAIL_EC 6
#define CF_MAX_GLOBS_EC 7

typedef void (*cf_target_fn)(void);
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

typedef struct {
    size_t c;
    char** p;
} cf_glob_t;

typedef enum {
    REGISTER_PHASE = 0,
    CONFIG_CONSTRUCT_PHASE = 1,
    TARGET_EXECUTE_PHASE = 2,
} cf_state_t;

static cf_target_decl_t cf_targets[CF_MAX_TARGETS] = { 0 };
static size_t cf_num_targets = 0;

static cf_config_decl_t cf_configs[CF_MAX_CONFIGS] = { 0 };
static size_t cf_num_configs = 0;

static glob_t cf_globs[CF_MAX_GLOBS] = { 0 };
static size_t cf_num_globs = 0;

static cf_state_t cf_state = REGISTER_PHASE;

static void cf_register_target(const char* name, cf_target_fn fn) {
    if (cf_state != REGISTER_PHASE) {
        CF_ERR_LOG("Error: Invalid cf_state (%d) when registering target!\n", cf_state);
        exit(CF_INVALID_STATE_EC);
    }

    if (strlen(name) > CF_MAX_NAME_LENGTH) {
        CF_ERR_LOG("Error: The name \"%s\" when registering target is too long (max name length: %d)\n", name, CF_MAX_NAME_LENGTH);
        exit(CF_NAME_TOO_LONG_EC);
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

    if (strlen(name) > CF_MAX_NAME_LENGTH) {
        CF_ERR_LOG("Error: The name \"%s\" when registering config is too long (max name length: %d)\n", name, CF_MAX_NAME_LENGTH);
        exit(CF_NAME_TOO_LONG_EC);
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

static cf_glob_t cf_glob(const char* expr) {
    glob_t glob_res = { 0 };
    uint32_t rc = glob(expr, GLOB_NOSORT | GLOB_MARK | GLOB_NOESCAPE, NULL, &glob_res);
    
    if (rc == GLOB_NOMATCH) {
        globfree(&glob_res);
        return (cf_glob_t) {
            .c = 0,
            .p = NULL,
        };
    } else if (rc == GLOB_NOSPACE) {
        CF_ERR_LOG("Error: glob() ran out of memory during cf_glob() call!\n");
        exit(CF_CLIB_FAIL_EC);
    } else if (rc == GLOB_ABORTED) {
        CF_ERR_LOG("Error: glob() aborted due to a read error during cf_glob() call!\n");
        exit(CF_CLIB_FAIL_EC);
    }

    if (cf_num_globs >= CF_MAX_GLOBS) {
        CF_ERR_LOG("Error: Maximum globs of %d was reached!\n", CF_MAX_GLOBS);
        exit(CF_MAX_GLOBS_EC);
    }

    cf_globs[cf_num_globs++] = glob_res;
    return (cf_glob_t) {
        .c = glob_res.gl_pathc,
        .p = glob_res.gl_pathv,
    };
}

static void cf_free_glob(size_t frame_size) {
    for (size_t i = frame_size; i > 0 && cf_num_globs > 0; i--) {
        globfree(&cf_globs[--cf_num_globs]);
        cf_globs[cf_num_globs] = (glob_t) { 0 };
    }
}

static void cf_depends_on(const char* name) {
    if (cf_state != TARGET_EXECUTE_PHASE) {
        CF_ERR_LOG("Error: Invalid cf_state (%d) when executing dependency!\n", cf_state);
        exit(CF_INVALID_STATE_EC);
    }

    for (int64_t i = cf_num_targets - 1; i >= 0; i--) {
        if (strncmp(name, cf_targets[i].name, CF_MAX_NAME_LENGTH) == 0) {
            if (!cf_targets[i].executed) {
                cf_targets[i].fn();
            }

            return;
        }
    }

    CF_ERR_LOG("Error: Could not find target \"%s\" during dependency resolution!\n", name);
    exit(CF_TARGET_NOT_FOUND_EC);
}

__attribute__((weak)) int main(int argc, char** argv) {
    (void) cf_register_config;
    (void) cf_register_target;
    (void) cf_depends_on;
    (void) cf_glob;

    if (argc == 1) {
        return CF_SUCCESS_EC;
    }

    cf_state = CONFIG_CONSTRUCT_PHASE;
    for (size_t i = 0; i < cf_num_configs; i++) {
        cf_configs[i].fn();
    }

    cf_state = TARGET_EXECUTE_PHASE;
    for (int32_t i = 1; i < argc; i++) {
        for (size_t j = 0; j < cf_num_targets; j++) {
            cf_target_decl_t* target = &cf_targets[j];
            if (strcmp(target->name, argv[i]) == 0) {
                if (target->executed) {
                    CF_WRN_LOG("Warning: Target \"%s\" was executed already! Skipping target...\n", argv[i]);
                    goto next_iter;
                }
                target->executed = true;
                target->fn();
                cf_free_glob(cf_num_globs);
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

#define CF_TARGET(name_ident) \
    static void cf_target_##name_ident(void); \
    __attribute__((constructor)) static void cf_target_reg_##name_ident(void) { \
        cf_register_target(#name_ident, cf_target_##name_ident); \
    } \
    static void cf_target_##name_ident(void)


#define CF_CONFIG(name_ident) \
    static void cf_config_##name_ident(void); \
    __attribute__((constructor)) static void cf_config_reg_##name_ident(void) { \
        cf_register_config(#name_ident, cf_config_##name_ident); \
    } \
    static void cf_config_##name_ident(void)

#define CF_DEPENDS_ON(name_ident) \
    cf_depends_on(#name_ident);

#define CF_GLOB(expr) \
    cf_glob(expr);

#define CF_GLOB_FOREACH(expr, filename, ...) \
    do { \
        size_t cf_saved_glob_frame_##filename = cf_num_globs; \
        cf_glob_t cf_glob_ret = cf_glob(expr); \
        for (size_t idx_##filename = 0; idx_##filename < cf_glob_ret.c; idx_##filename++) { \
            const char* filename = cf_glob_ret.p[idx_##filename]; \
            __VA_ARGS__ \
        }; \
        cf_free_glob(cf_num_globs - cf_saved_glob_frame_##filename); \
    } while (0);

#endif // CFORGE_H
