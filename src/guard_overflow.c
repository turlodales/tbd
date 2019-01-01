//
//  src/guard_overflow.c
//  tbd
//
//  Created by inoahdev on 12/29/18.
//  Copyright © 2018 inoahdev. All rights reserved.
//

#include "guard_overflow.h"

int guard_overflow_add_uint32(uint32_t *const left_in, const uint32_t right) {
    uint32_t left = *left_in;
    uint32_t result = left + right;

    if (result < left) {
        return 1;
    }

    *left_in = result;
    return 0;
}

int guard_overflow_add_uint64(uint64_t *const left_in, const uint64_t right) {
    uint64_t left = *left_in;
    uint64_t result = left + right;

    if (result < left) {
        return 1;
    }

    *left_in = result;
    return 0;
}

int guard_overflow_mul_uint32(uint32_t *const left_in, const uint32_t right) {
    uint32_t left = *left_in;
    uint32_t result = left * right;

    if ((result / right) != left) {
        return 1;
    }

    *left_in = result;
    return 0;
}

int guard_overflow_mul_uint64(uint64_t *const left_in, const uint64_t right) {
    uint64_t left = *left_in;
    uint64_t result = left * right;

    if ((result / right) != left) {
        return 1;
    }

    *left_in = result;
    return 0;
}
