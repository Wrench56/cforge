#include "cforge.h"

#include <stdio.h>

CF_CONFIG(debug) {
    printf("Config...\n");
}

CF_TARGET(link) {
    printf("Linking...\n");
}

CF_TARGET(build) {
    CF_DEPENDS_ON(link);
    printf("Building...\n");
}

/* =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= */
/*                                                     */
/*  CForge Build Tool                                  */
/*                                                     */
/* =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= */
/*                                                     */
/*  Author : Mark Devenyi                              */
/*  Source : https://github.com/Wrench56/cforge        */
/*  License: MIT License                               */
/*                                                     */
/* =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= */

