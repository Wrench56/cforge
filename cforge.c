#define CF_DISABLE_FILE_HASH

#include "cforge.h"

#include <stdio.h>

#if CF_VERSION_BELOW(1, 0, 0)
    #error "CForge too old!"
#endif

CF_CONFIG(release) {
    CF_SET_ENV(cflags, "-O2");
    CF_SET_ENV(mode, "release");
}

CF_CONFIG(debug) {
    CF_SET_ENV(cflags, "-g");
    CF_SET_ENV(mode, "debug");
}

CF_TARGET(build, CF_WITH_CONFIG(release), CF_DEPENDS(link), CF_HELP_STRING("Use this")) {
    printf("Build mode: %s\n", CF_ENV(mode));
    CF_NOP();
}

CF_TARGET(link, CF_DEPENDS(compile), CF_HIDDEN) {
    printf("Link mode: %s\n", CF_ENV(mode));
    if CF_FILE_NOT_UTD("app") {
        printf("Linking...\n");
        char* object_files = CF_JOIN_GLOB(CF_GLOB("build/*.o"), " ");
        printf("[" CF_CYAN "LD" CF_RESET "] %s\n", object_files);
        CF_RUN("touch %s", "app");
        //CF_RUN("ld %s", object_files)
        CF_FILE_MARK_UTD("app");
    }
}

CF_TARGET(compile, CF_HIDDEN, CF_VERBOSE) {
    printf("Compile mode: %s\n", CF_ENV(mode));
    CF_REMOVE("build/");
    CF_MKDIR("build/");
    for CF_GLOBS_EACH("playground/*.c", file) {
        char* output = CF_MAP(file, CF_MAP_EXT("o"), CF_MAP_PARENT("build"));
        if (CF_FILE_NOT_UTD(file) || CF_FILE_NOT_UTD(output)) {
            CF_BANNER("Compiling...");
            printf("[" CF_YELLOW "CC" CF_RESET "] %s\n", file);
            CF_RUNP("cc %s %s -o %s",
                CF_ENV(cflags),
                file,
                output
            );
            CF_FILE_MARK_UTDP(file);
            CF_FILE_MARK_UTDP(output);
        }
    }
}
