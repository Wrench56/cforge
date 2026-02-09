#ifndef CFORGE_H
#define CFORGE_H

#if 0
cc -O2 -Wall -Wextra "cforge.c" -o "./.b" && exec "./.b" "$@"
exit 0
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CF_MAX_TARGETS 64
#define CF_MAX_CONFIGS 64
#define CF_MAX_RULES 256

#define CF_MAX_FILES 1024
#define CF_MAX_INGLOBS 64
#define CF_MAX_INS 64
#define CF_MAX_OUTS 64

#define CF_ERR_LOG(...) fprintf(stderr, __VA_ARGS__)
#define CF_WRN_LOG(...) fprintf(stdout, __VA_ARGS__)

#define CF_SUCCESS_EC 0
#define CF_MAX_TARGETS_EC 1
#define CF_MAX_CONFIGS_EC 2
#define CF_INVALID_STATE_EC 3
#define CF_TARGET_DOUBLE_EXECUTED 4
#define CF_MAX_RULES_EC 5
#define CF_MAX_FILES_EC 6
#define CF_MAX_INS_EC 7
#define CF_MAX_INGLOBS_EC 8
#define CF_MAX_OUTS_EC 9

#define CF_MAX(a, b) (((a) > (b)) ? (a) : (b))
#define CF_SIZE_OF_ARR(arr) (sizeof(arr) / sizeof((arr)[0]))

struct cf_target_decl;

typedef void (*cf_target_fn)(struct cf_target_decl* target);
typedef void (*cf_config_fn)(void);

typedef enum {
    REGISTER_PHASE = 0,
    CONFIG_CONSTRUCT_PHASE = 1,
    TARGET_CONSTRUCT_PHASE = 2
} cf_state_t;

typedef enum {
    CF_MODIF_INVALID = 0,
    CF_MODIF_INS,
    CF_MODIF_INGLOBS,
    CF_MODIF_OUTS,
    CF_MODIF_CMD,
} cf_rule_modif_kind_t;

typedef struct cf_target_decl {
    const char* name;
    cf_target_fn fn;
    bool evaluated;
} cf_target_decl_t;

typedef struct {
    const char* name;
    cf_config_fn fn;
} cf_config_decl_t;

typedef struct {
    cf_rule_modif_kind_t kind;
    union { 
        struct {
            char* filenames[CF_MAX(CF_MAX_INS, CF_MAX_OUTS)];
            size_t size;
        } filelist;
        struct {
            char* inglobs[CF_MAX_INGLOBS];
            size_t size;
        } ingloblist;
    } args;
} cf_rule_modif_t;

typedef struct {
    size_t filename_idx;
} cf_file_t;

typedef struct {
    cf_target_decl_t* target_ptr;
    cf_file_t ins[CF_MAX_INS];
    cf_file_t outs[CF_MAX_OUTS];
    size_t inglobs_str[CF_MAX_INGLOBS];
    size_t ins_size;
    size_t outs_size;
    size_t inglobs_size;
} cf_rule_t;

static cf_target_decl_t cf_targets[CF_MAX_TARGETS] = { 0 };
static size_t cf_num_targets = 0;

static cf_config_decl_t cf_configs[CF_MAX_CONFIGS] = { 0 };
static size_t cf_num_configs = 0;

static cf_rule_t cf_rules[CF_MAX_RULES] = { 0 };
static size_t cf_num_rules = 0;

static char* cf_file_strings[CF_MAX_FILES] = { 0 };
static size_t cf_num_file_strings = 0;

static char* cf_inglob_strings[CF_MAX_INGLOBS] = { 0 };
static size_t cf_num_inglob_strings = 0;

static cf_state_t cf_state = REGISTER_PHASE;

static ssize_t cf_find_str(char** list, size_t n, char* lookup) {
    for (size_t i = 0; i < n; i++) {
        if (strcmp(list[i], lookup) == 0) {
            return (ssize_t) i;
        }
    }

    return -1;
}

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
        .evaluated = false
    };
}

static void cf_register_config(const char* name, cf_config_fn fn) {
    if (cf_state != REGISTER_PHASE) {
        CF_ERR_LOG("Error: Invalid cf_state (%d) when registering config!\n", cf_state);
        exit(CF_INVALID_STATE_EC);
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

static void cf_register_rule(cf_target_decl_t* target, const cf_rule_modif_t modifs[], size_t size) {
    cf_rule_t rule = { 0 };
    rule.target_ptr = target;

    for (size_t i = 0; i < size; i++) {
        cf_rule_modif_t modif = modifs[i];
        switch (modif.kind) {
            case CF_MODIF_INS:
                for (size_t i = 0; i < modif.args.filelist.size; i++) {
                    ssize_t idx = cf_find_str(
                            cf_file_strings,
                            cf_num_file_strings,
                            modif.args.filelist.filenames[i]
                    );

                    if (idx == -1) {
                        idx = cf_num_file_strings;
                        if (cf_num_file_strings >= CF_MAX_FILES) {
                            CF_ERR_LOG("Error: Reached maximum number of files!\n");
                            exit(CF_MAX_FILES_EC);

                        }
                        cf_file_strings[cf_num_file_strings++] = modif.args.filelist.filenames[i];
                    }

                    if (rule.ins_size >= CF_MAX_INS) {
                            CF_ERR_LOG("Error: Reached maximum number of in-type dependencies!\n");
                            exit(CF_MAX_INS_EC);

                    }
                    rule.ins[rule.ins_size++] = (cf_file_t) { .filename_idx = (size_t) idx };
                }

                break;
            case CF_MODIF_INGLOBS:
                for (size_t i = 0; i < modif.args.ingloblist.size; i++) {
                    ssize_t idx = cf_find_str(
                            cf_inglob_strings,
                            cf_num_inglob_strings,
                            modif.args.ingloblist.inglobs[i]
                    );

                    if (idx == -1) {
                        idx = cf_num_inglob_strings;
                        if (cf_num_inglob_strings >= CF_MAX_INGLOBS) {
                            CF_ERR_LOG("Error: Reached maximum number of globs!\n");
                            exit(CF_MAX_INGLOBS_EC);

                        }
                        cf_inglob_strings[cf_num_inglob_strings++] = modif.args.ingloblist.inglobs[i];
                    }

                    if (rule.inglobs_size >= CF_MAX_INGLOBS) {
                            CF_ERR_LOG("Error: Reached maximum number of inglob-type dependencies!\n");
                            exit(CF_MAX_INGLOBS_EC);
                    }
                    rule.inglobs_str[rule.inglobs_size++] = idx;
                }

                break;
            case CF_MODIF_OUTS:
                for (size_t i = 0; i < modif.args.filelist.size; i++) {
                    ssize_t idx = cf_find_str(
                            cf_file_strings,
                            cf_num_file_strings,
                            modif.args.filelist.filenames[i]
                    );

                    if (idx == -1) {
                        idx = cf_num_file_strings;
                        if (cf_num_file_strings >= CF_MAX_FILES) {
                            CF_ERR_LOG("Error: Reached maximum number of files!\n");
                            exit(CF_MAX_FILES_EC);

                        }
                        cf_file_strings[cf_num_file_strings++] = modif.args.filelist.filenames[i];
                    }

                    if (rule.outs_size >= CF_MAX_OUTS) {
                            CF_ERR_LOG("Error: Reached maximum number of out-type dependencies!\n");
                            exit(CF_MAX_OUTS_EC);
                    }
                    rule.outs[rule.outs_size++] = (cf_file_t) { .filename_idx = (size_t) idx };
                }

                break;
            case CF_MODIF_CMD:
                break;
            case CF_MODIF_INVALID:
                CF_ERR_LOG("Error: Invalid modifier used when constructing target \"%s\"!\n", target->name);
                break;
        }
    }

    if (cf_num_rules >= CF_MAX_RULES) {
        CF_ERR_LOG("Error: Reached maximum number of rules!\n");
        exit(CF_MAX_RULES_EC);
    }
    cf_rules[cf_num_rules++] = rule;
}

__attribute__((weak)) int main(int argc, char** argv) {
    (void) cf_register_config;
    (void) cf_register_target;
    (void) argc;
    (void) argv;

    if (argc == 1) {
        return CF_SUCCESS_EC;
    }

    cf_state = CONFIG_CONSTRUCT_PHASE;
    for (size_t i = 0; i < cf_num_configs; i++) {
        cf_configs[i].fn();
    }

    cf_state = TARGET_CONSTRUCT_PHASE;
    for (size_t i = 0; i < cf_num_targets; i++) {
        cf_target_decl_t* target = &cf_targets[i];
        if (target->evaluated) {
            CF_ERR_LOG("Error: Target \"%s\" was evaluated already! This should never happen!\n", target->name);
            exit(CF_TARGET_DOUBLE_EXECUTED);
        }

        target->evaluated = true;
        target->fn(target);
    }

    return CF_SUCCESS_EC;
}

#define CF_TARGET(name_ident) \
    static void cf_target_##name_ident(cf_target_decl_t* target); \
    __attribute__((constructor)) static void cf_target_reg_##name_ident(void) { \
        cf_register_target(#name_ident, cf_target_##name_ident); \
    } \
    static void cf_target_##name_ident(cf_target_decl_t* target)


#define CF_CONFIG(name_ident) \
    static void cf_config_##name_ident(void); \
    __attribute__((constructor)) static void cf_config_reg_##name_ident(void) { \
        cf_register_config(#name_ident, cf_config_##name_ident); \
    } \
    static void cf_config_##name_ident(void)

#define CF_RULE(...) \
    do { \
        const cf_rule_modif_t cf_rule_modifs[] = { __VA_ARGS__ }; \
        cf_register_rule(target, cf_rule_modifs, CF_SIZE_OF_ARR(cf_rule_modifs)); \
    } while (0);

#define CF_INS(...) \
    (cf_rule_modif_t) { \
        .kind = CF_MODIF_INS, \
        .args.filelist = { \
            .filenames = { __VA_ARGS__ }, \
            .size = CF_SIZE_OF_ARR((const char*[]) {__VA_ARGS__}) \
        } \
    }

#define CF_INGLOBS(...) \
    (cf_rule_modif_t) { \
        .kind = CF_MODIF_INGLOBS, \
        .args.ingloblist = { \
            .inglobs = { __VA_ARGS__ }, \
            .size = CF_SIZE_OF_ARR((const char*[]) {__VA_ARGS__}) \
        } \
    }

#define CF_OUTS(...) \
    (cf_rule_modif_t) { \
        .kind = CF_MODIF_OUTS, \
        .args.filelist = { \
            .filenames = { __VA_ARGS__ }, \
            .size = CF_SIZE_OF_ARR((const char*[]) {__VA_ARGS__}) \
        } \
    }

#endif // CFORGE_H
