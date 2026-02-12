#ifndef CFORGE_H
#define CFORGE_H

#if 0
cc -O2 -Wall -Wextra -Wshadow -Wpedantic -Wconversion -Wstrict-prototypes -Wformat=2 -Wmissing-prototypes -Wold-style-definition -Wdouble-promotion -Wno-unused-parameter "cforge.c" -o "./.b" && exec "./.b" "$@"
exit 0
#endif

#include <glob.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* TODO: Add other threading implementations (pthreads, fork, WinAPI) */
#include <threads.h>

#define CF_MAX_TARGETS 64
#define CF_MAX_CONFIGS 64
#define CF_MAX_GLOBS 64
#define CF_MAX_THRDS 16
#define CF_MAX_ENVS 256
#define CF_MAX_JOIN_STRINGS 256
#define CF_MAX_JOIN_STRING_LEN 8196

#define CF_MAX_NAME_LENGTH 127
#define CF_MAX_COMMAND_LENGTH 1 * 1024

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
#define CF_MAX_COMMAND_LENGTH_EC 8
#define CF_MAX_THRDS_EC 9
#define CF_TARGET_DEP_CYCLE_EC 10
#define CF_CONFIG_NOT_FOUND_EC 11
#define CF_UNKNOWN_ATTR_EC 12
#define CF_MAX_ENVS_EC 13
#define CF_MAX_JSTRINGS_EC 14

typedef void (*cf_target_fn)(void);
typedef void (*cf_config_fn)(void);


typedef enum {
    UNKNOWN = 0,
    DEPENDENCY,
    CONFIG_SET
} cf_attr_type_t;

typedef struct {
    cf_attr_type_t type;
    union {
        struct {
            const char* target_name;
        } depends;
        struct {
            const char* config_name;
        } configset;
    } arg;
} cf_attr_t;

typedef enum {
    UNVISITED = 0,
    VISITING,
    DONE
} cf_dfs_node_status_t;

typedef struct {
    const char* name;
    cf_target_fn fn;
    cf_attr_t* attribs;
    size_t attribs_size;
    cf_dfs_node_status_t node_status;
} cf_target_decl_t;

typedef struct {
    const char* name;
    cf_config_fn fn;
} cf_config_decl_t;

typedef struct {
    const char* envname;
    char* value;
    bool was_set;
} cf_env_restore_t;

typedef struct {
    size_t c;
    char** p;
} cf_glob_t;

typedef enum {
    REGISTER_PHASE = 0,
    TARGET_PLAN_PHASE = 1,
    TARGET_EXECUTE_PHASE = 2,
} cf_state_t;

static cf_target_decl_t cf_targets[CF_MAX_TARGETS] = { 0 };
static size_t cf_num_targets = 0;

static cf_config_decl_t cf_configs[CF_MAX_CONFIGS] = { 0 };
static size_t cf_num_configs = 0;

static glob_t cf_globs[CF_MAX_GLOBS] = { 0 };
static size_t cf_num_globs = 0;

static thrd_t cf_thrd_pool[CF_MAX_THRDS] = { 0 };
static size_t cf_num_thrds = 0;

static cf_env_restore_t cf_envs[CF_MAX_ENVS] = { 0 };
static size_t cf_num_envs = 0;

static char* cf_jstrings[CF_MAX_JOIN_STRINGS] = { 0 };
static size_t cf_num_jstrings = 0;

static cf_state_t cf_state = REGISTER_PHASE;

static size_t cf_find_target_index(const char* target_name) {
    for (size_t i = cf_num_targets; i-- > 0;) {
        if (strncmp(target_name, cf_targets[i].name, CF_MAX_NAME_LENGTH) == 0) {
            return i;
        }
    }

    return CF_MAX_TARGETS + 1;
}

static void cf_register_target(const char* name, cf_target_fn fn, const cf_attr_t* attribs, size_t attribs_size) {
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

    void* attribs_block;
    if (attribs_size > 0) {
        attribs_block = malloc(attribs_size * sizeof(cf_attr_t));
        if (attribs_block == NULL) {
            CF_ERR_LOG("Error: malloc() failed in cf_register_target()\n");
            exit(CF_CLIB_FAIL_EC);
        }

        memcpy(attribs_block, attribs, attribs_size * sizeof(cf_attr_t));
    } else {
        attribs_block = NULL;
    }

    cf_targets[cf_num_targets++] = (cf_target_decl_t) {
        .name = name,
        .fn = fn,
        .attribs = (cf_attr_t*) attribs_block,
        .attribs_size = attribs_size,
        .node_status = UNVISITED
    };
}

static void cf_free_glob(size_t checkpoint);
static void cf_free_jstrings(size_t checkpoint);
static void cf_restore_env(size_t env_checkpoint);
static void cf_dfs_execute(cf_target_decl_t* target) {
    if (target->node_status == DONE) {
        return;
    } else if (target->node_status == VISITING) {
        CF_ERR_LOG("Error: Dependency cycle detected for \"%s\"\n", target->name);
        exit(CF_TARGET_DEP_CYCLE_EC);
    }

    target->node_status = VISITING;
    cf_config_decl_t* config = NULL;
    for (size_t i = 0; i < target->attribs_size; i++) {
        cf_attr_t* attrib = &target->attribs[i];
        switch (attrib->type) {
            case DEPENDENCY: {
                const char* dep_target_name = attrib->arg.depends.target_name;
                size_t dep_idx = cf_find_target_index(dep_target_name);
                if (dep_idx >= cf_num_targets) {
                    CF_ERR_LOG("Error: Target \"%s\" not found!\n", dep_target_name);
                    exit(CF_TARGET_NOT_FOUND_EC);
                }
                
                cf_dfs_execute(&cf_targets[dep_idx]);
                break;
            }
            case CONFIG_SET: {
                if (config != NULL) {
                    CF_WRN_LOG("Warning: Cannot set two or more configs per target. Ignoring...\n");
                    goto next_attr;
                }
                
                const char* conf_name = attrib->arg.configset.config_name;
                for (size_t c_idx = 0; c_idx < cf_num_configs; c_idx++) {
                    if (strncmp(conf_name, cf_configs[c_idx].name, CF_MAX_NAME_LENGTH) == 0) {
                        config = &cf_configs[c_idx];
                        goto next_attr;
                    }
                }
                
                CF_ERR_LOG("Error: Config \"%s\" not found!\n", conf_name);
                exit(CF_CONFIG_NOT_FOUND_EC);
                break;
            }
            case UNKNOWN: {
                CF_ERR_LOG("Error: Unknown attribute given for target \"%s\"\n", target->name);
                exit(CF_UNKNOWN_ATTR_EC);
            }
        }

next_attr:
        continue;
    }

    size_t env_checkpoint = cf_num_envs;
    if (config != NULL) {
        config->fn();
    }

    size_t glob_checkpoint = cf_num_globs;
    size_t jstrings_checkpoint = cf_num_jstrings;
    target->fn();
    cf_free_jstrings(jstrings_checkpoint);
    cf_free_glob(glob_checkpoint);
    cf_restore_env(env_checkpoint);
    target->node_status = DONE;
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

    if (cf_num_configs >= CF_MAX_CONFIGS) {
        CF_ERR_LOG("Error: Maximum configs of %d was reached!\n", CF_MAX_CONFIGS);
        exit(CF_MAX_CONFIGS_EC);
    }

    cf_configs[cf_num_configs++] = (cf_config_decl_t) {
        .name = name,
        .fn = fn
    };
}

static void cf_setenv_wrapper(const char* ident, char* value) {
    if (cf_num_envs >= CF_MAX_ENVS) {
        CF_ERR_LOG("Error: Maximum environment variables of %d was reached!\n", CF_MAX_ENVS);
        exit(CF_MAX_ENVS_EC);
    }

    char* envvar = getenv(ident);
    if (envvar == NULL) {
        cf_envs[cf_num_envs++] = (cf_env_restore_t) {
            .envname = ident,
            .value = NULL,
            .was_set = false
        };
    } else {
        char* value_block = (char*) malloc(strlen(envvar) + 1);
        if (value_block == NULL) {
            CF_ERR_LOG("Error: malloc() failed in cf_setenv_wrapper()\n");
            exit(CF_CLIB_FAIL_EC);
        }

        strcpy(value_block, envvar);
        cf_envs[cf_num_envs++] = (cf_env_restore_t) {
            .envname = ident,
            .value = value_block,
            .was_set = true
        };
    }

    if (setenv(ident, value, 1) != 0) {
        CF_ERR_LOG("Error: setenv() failed in cf_setenv_wrapper()\n");
        exit(CF_CLIB_FAIL_EC);
    }
}

static void cf_restore_env(size_t env_checkpoint) {
    cf_env_restore_t envres;
    while (cf_num_envs > env_checkpoint) {
        envres = cf_envs[--cf_num_envs];
        if (!envres.was_set) {
            if (unsetenv(envres.envname) != 0) {
                CF_ERR_LOG("Error: unsetenv() failed in cf_restore_env()\n");
                exit(CF_CLIB_FAIL_EC);
            }
        } else {
            if (setenv(envres.envname, envres.value, 1) != 0) {
                CF_ERR_LOG("Error: setenv() failed in cf_restore_env()\n");
                exit(CF_CLIB_FAIL_EC);
            }
        }

        free(envres.value);
    }
}

static cf_glob_t cf_glob(const char* expr) {
    glob_t glob_res = { 0 };
    int32_t rc = glob(expr, GLOB_NOSORT | GLOB_MARK | GLOB_NOESCAPE, NULL, &glob_res);
    
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

static void cf_free_glob(size_t checkpoint) {
    while (cf_num_globs > checkpoint) {
        globfree(&cf_globs[--cf_num_globs]);
        cf_globs[cf_num_globs] = (glob_t) { 0 };
    }
}

static char* cf_join(char* strings[], char* separator, size_t length) {
    if (length < 1) {
        return (char*) "";
    }

    if (cf_num_jstrings >= CF_MAX_JOIN_STRINGS) {
        CF_ERR_LOG("Error: Maximum joined strings of %d was reached!\n", CF_MAX_JOIN_STRINGS);
        exit(CF_MAX_JSTRINGS_EC);
    }

    char* jstring = (char*) malloc(CF_MAX_JOIN_STRING_LEN);
    if (jstring == NULL) {
        CF_ERR_LOG("Error: malloc() failed in cf_join()\n");
        exit(CF_CLIB_FAIL_EC);
    }

    cf_jstrings[cf_num_jstrings++] = jstring;

    const char* endptr = jstring + CF_MAX_JOIN_STRING_LEN - 1;
    char* cptr = stpncpy(jstring, strings[0], (size_t) (endptr - jstring));
    for (size_t i = 1; i < length; i++) {
        cptr = stpncpy(cptr, separator, (size_t) (endptr - cptr));
        cptr = stpncpy(cptr, strings[i], (size_t) (endptr - cptr));
    }

    *cptr = '\0';
    return jstring;
}

static void cf_free_jstrings(size_t checkpoint) {
    while (cf_num_jstrings > checkpoint) {
        free(cf_jstrings[--cf_num_jstrings]);
        cf_jstrings[cf_num_jstrings] = NULL;
    }
}

static int cf_thrd_helper(void* args) {
    if (system((char*) args) != 0) {
        CF_ERR_LOG("Error: Executing command \"%s\" failed\n", (char*) args);
        exit(CF_CLIB_FAIL_EC);
    }
    free(args);
    return 0;
}

static void cf_execute_command(bool is_parallel, char* buffer) {
    if (is_parallel) {
        if (cf_num_thrds >= CF_MAX_THRDS) {
            CF_ERR_LOG("Error: Maximum threads of %d was reached!\n", CF_MAX_THRDS);
            exit(CF_MAX_THRDS_EC);
        }

        thrd_t worker_thread;
        if (thrd_create(&worker_thread, &cf_thrd_helper, (void*) buffer) != thrd_success) {
            CF_ERR_LOG("Error: Thread failed during creation in cf_execute_command()\n");
            exit(CF_CLIB_FAIL_EC);
        }

        cf_thrd_pool[cf_num_thrds++] = worker_thread;
        return;
    }

    if (system(buffer) != 0) {
        CF_ERR_LOG("Error: Executing command \"%s\" failed", (char*) buffer);
        exit(CF_CLIB_FAIL_EC);
    }

    free(buffer);
}

__attribute__((weak)) int main(int argc, char** argv) {
    (void) cf_register_config;
    (void) cf_register_target;
    (void) cf_glob;
    (void) cf_join;

    if (argc == 1) {
        goto cleanup;
    }

    cf_state = TARGET_EXECUTE_PHASE;
    for (int32_t i = 1; i < argc; i++) {
        for (size_t j = 0; j < cf_num_targets; j++) {
            cf_target_decl_t* target = &cf_targets[j];
            if (strcmp(target->name, argv[i]) == 0) {
                if (target->node_status == DONE) {
                    CF_WRN_LOG("Warning: Target \"%s\" was executed already! Skipping target...\n", argv[i]);
                    goto next_iter;
                }
                cf_dfs_execute(target);
                for (size_t t = cf_num_thrds; t > 0; t--) {
                    thrd_join(cf_thrd_pool[t - 1], NULL);
                    cf_thrd_pool[t - 1] = (thrd_t) { 0 };
                }

                cf_num_thrds = 0;
                goto next_iter;
            }
        }

        CF_ERR_LOG("Error: Target \"%s\" not found!\n", argv[i]);
        return CF_TARGET_NOT_FOUND_EC;

        next_iter:
            continue;
    }

cleanup:
    for (size_t t_idx = 0; t_idx < cf_num_targets; t_idx++) {
        free(cf_targets[t_idx].attribs);
    }

    return CF_SUCCESS_EC;
}

#define CF_TARGET(name_ident, ...) \
    static void cf_target_##name_ident(void); \
    __attribute__((constructor)) static void cf_target_reg_##name_ident(void) { \
        const cf_attr_t attribs[] = { __VA_ARGS__ }; \
        cf_register_target(#name_ident, cf_target_##name_ident, attribs, sizeof(attribs)/sizeof(cf_attr_t)); \
    } \
    static void cf_target_##name_ident(void)


#define CF_CONFIG(name_ident) \
    static void cf_config_##name_ident(void); \
    __attribute__((constructor)) static void cf_config_reg_##name_ident(void) { \
        cf_register_config(#name_ident, cf_config_##name_ident); \
    } \
    static void cf_config_##name_ident(void)

#define CF_GLOB(expr) \
    cf_glob(expr);

#define CF_GLOB_FOREACH(expr, filename, ...) \
    do { \
        size_t cf_saved_glob_checkpoint_##filename = cf_num_globs; \
        cf_glob_t cf_glob_ret = cf_glob(expr); \
        for (size_t idx_##filename = 0; idx_##filename < cf_glob_ret.c; idx_##filename++) { \
            const char* filename = cf_glob_ret.p[idx_##filename]; \
            __VA_ARGS__ \
        }; \
        cf_free_glob(cf_saved_glob_checkpoint_##filename); \
    } while (0);

#define CF_INTERNAL_RUNNER(parallel, format_str, ...) \
    do { \
        char* buffer = malloc(CF_MAX_COMMAND_LENGTH); \
        if (buffer == NULL) { \
            CF_ERR_LOG("Error: malloc() failed in CF_INTERNAL_RUNNER\n"); \
            exit(CF_CLIB_FAIL_EC); \
        } \
        int n = snprintf(buffer, CF_MAX_COMMAND_LENGTH, format_str, ##__VA_ARGS__); \
        if (n < 0) { \
            CF_ERR_LOG("Error: snprintf() failed in CF_INTERNAL_RUNNER\n"); \
            exit(CF_CLIB_FAIL_EC); \
        }else if (n >= CF_MAX_COMMAND_LENGTH) { \
            CF_ERR_LOG("Error: Maximum command length of %d was reached!\n", CF_MAX_COMMAND_LENGTH); \
            exit(CF_MAX_COMMAND_LENGTH_EC); \
        } \
        cf_execute_command(parallel, buffer); \
    } while (0);

#define CF_RUNP(format_str, ...) CF_INTERNAL_RUNNER(true, format_str, ##__VA_ARGS__)
#define CF_RUN(format_str, ...) CF_INTERNAL_RUNNER(false, format_str, ##__VA_ARGS__)

#define CF_DEPENDS(target_ident) \
    (cf_attr_t) { \
        .type = DEPENDENCY, \
        .arg.depends = { \
            .target_name = #target_ident \
        } \
    }

#define CF_WITH_CONFIG(config_ident) \
    (cf_attr_t) { \
        .type = CONFIG_SET, \
        .arg.configset = { \
            .config_name = #config_ident \
        } \
    }

#define CF_SET_ENV(ident, value) cf_setenv_wrapper(#ident, value);
#define CF_ENV(ident) getenv(#ident)

#endif // CFORGE_H
