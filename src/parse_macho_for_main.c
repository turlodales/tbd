//
//  src/parse_macho_for_main.c
//  tbd
//
//  Created by inoahdev on 12/01/18.
//  Copyright © 2018 - 2019 inoahdev. All rights reserved.
//

#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "handle_macho_file_parse_result.h"
#include "macho_file.h"
#include "our_io.h"
#include "parse_macho_for_main.h"
#include "recursive.h"

static void
clear_create_info(struct tbd_create_info *__notnull const info_in,
                  const struct tbd_create_info *__notnull const orig)
{
    tbd_create_info_clear(info_in);
    const struct array exports = info_in->fields.exports;

    *info_in = *orig;
    info_in->fields.exports = exports;
}

static int
read_magic(void *__notnull const magic_in,
           uint64_t *__notnull const magic_in_size_in,
           const int fd)
{
    const uint64_t magic_in_size = *magic_in_size_in;
    if (magic_in_size >= sizeof(uint32_t)) {
        return 0;
    }

    const uint64_t read_size = sizeof(uint32_t) - magic_in_size;
    if (our_read(fd, magic_in + magic_in_size, read_size) < 0) {
        return 1;
    }

    *magic_in_size_in = sizeof(uint32_t);
    return 0;
}

static void verify_write_path(const struct tbd_for_main *__notnull const tbd) {
    const char *const write_path = tbd->write_path;
    if (write_path == NULL) {
        return;
    }

    struct stat sbuf = {};
    if (stat(write_path, &sbuf) < 0) {
        /*
         * The write-file doesn't have to exist.
         */

        if (errno != ENOENT) {
            fprintf(stderr,
                    "Failed to get information on object at the provided "
                    "write-path (%s), error: %s\n",
                    write_path,
                    strerror(errno));

            exit(1);
        }

        return;
    }

    if (!S_ISREG(sbuf.st_mode)) {
        fprintf(stderr,
                "Writing to a regular file while parsing a mach-o file "
                "(at path %s) is not supported",
                tbd->parse_path);

        exit(1);
    }
}

static FILE *
open_file_for_path(const struct parse_macho_for_main_args *__notnull const args,
                   char *__notnull const write_path,
                   const uint64_t write_path_length,
                   char **__notnull const terminator_out)
{
    char *terminator = NULL;

    const struct tbd_for_main *const tbd = args->tbd;
    const uint64_t options = tbd->flags;

    const int flags = (options & F_TBD_FOR_MAIN_NO_OVERWRITE) ? O_EXCL : 0;
    const int write_fd =
        open_r(write_path,
               write_path_length,
               O_WRONLY | O_TRUNC | flags,
               DEFFILEMODE,
               0755,
               &terminator);

    if (write_fd < 0) {
        /*
         * Although getting the file descriptor failed, its likely open_r still
         * created the directory hierarchy, and if so the terminator shouldn't
         * be NULL.
         */

        if (terminator != NULL) {
            /*
             * Ignore the return value as we cannot be sure if the remove failed
             * as the directories we created (that are pointed to by terminator)
             * may now be populated with other files.
             */

            remove_file_r(write_path, write_path_length, terminator);
        }

        if (!(options & F_TBD_FOR_MAIN_IGNORE_WARNINGS)) {
            /*
             * If the file already exists, we should just skip over to prevent
             * overwriting.
             *
             * Note:
             * EEXIST is only returned when O_EXCL was set, which is only
             * set for F_TBD_FOR_MAIN_NO_OVERWRITE.
             */

            if (errno == EEXIST) {
                if (tbd->flags & F_TBD_FOR_MAIN_IGNORE_WARNINGS) {
                    return NULL;
                }

                if (args->print_paths) {
                    fprintf(stderr,
                            "Skipping over file (at path %s) as a file "
                            "at its write-path (%s) already exists\n",
                            args->dir_path,
                            write_path);
                } else {
                    fputs("Skipping over file at provided-path as a file "
                            "at its provided write-path already exists\n",
                            stderr);
                }

                return NULL;
            }

            if (args->print_paths) {
                fprintf(stderr,
                        "Failed to open write-file (for path: %s), error: %s\n",
                        write_path,
                        strerror(errno));
            } else {
                fprintf(stderr,
                        "Failed to open the provided write-file, error: %s\n",
                        strerror(errno));
            }
        }

        return NULL;
    }

    FILE *const file = fdopen(write_fd, "w");
    if (file == NULL) {
        if (!(options & F_TBD_FOR_MAIN_IGNORE_WARNINGS)) {
            if (args->print_paths) {
                fprintf(stderr,
                        "Failed to open write-file (for path: %s) as FILE, "
                        "error: %s\n",
                        write_path,
                        strerror(errno));
            } else {
                fprintf(stderr,
                        "Failed to open the provided write-file as FILE, "
                        "error: %s\n",
                        strerror(errno));
            }
        }
    }

    *terminator_out = terminator;
    return file;
}

static FILE *
open_file_for_path_while_recursing(
    struct parse_macho_for_main_args *const args,
    char *__notnull const write_path,
    const uint64_t write_path_length,
    char **__notnull const terminator_out)
{
    FILE *file = args->combine_file;
    if (file != NULL) {
        return file;
    }

    char *terminator = NULL;

    const struct tbd_for_main *const tbd = args->tbd;
    const uint64_t options = tbd->flags;

    const int flags = (options & F_TBD_FOR_MAIN_NO_OVERWRITE) ? O_EXCL : 0;
    const int write_fd =
        open_r(write_path,
               write_path_length,
               O_WRONLY | O_TRUNC | flags,
               DEFFILEMODE,
               0755,
               &terminator);

    if (write_fd < 0) {
        /*
         * Although getting the file descriptor failed, its likely open_r still
         * created the directory hierarchy, and if so the terminator shouldn't
         * be NULL.
         */

        if (terminator != NULL) {
            /*
             * Ignore the return value as we cannot be sure if the remove failed
             * as the directories we created (that are pointed to by terminator)
             * may now be populated with other files.
             */

            remove_file_r(write_path, write_path_length, terminator);
        }

        if (!(options & F_TBD_FOR_MAIN_IGNORE_WARNINGS)) {
            /*
             * If the file already exists, we should just skip over to prevent
             * overwriting.
             *
             * Note:
             * EEXIST is only returned when O_EXCL was set, which is only
             * set for F_TBD_FOR_MAIN_NO_OVERWRITE.
             */

            if (errno == EEXIST) {
                if (tbd->flags & F_TBD_FOR_MAIN_IGNORE_WARNINGS) {
                    return NULL;
                }

                if (args->print_paths) {
                    fprintf(stderr,
                            "Skipping over file (at path %s/%s) as a file "
                            "at its write-path (%s) already exists\n",
                            args->dir_path,
                            args->name,
                            write_path);
                } else {
                    fputs("Skipping over file at provided-path as a file "
                            "at its provided write-path already exists\n",
                            stderr);
                }

                return NULL;
            }

            if (args->print_paths) {
                fprintf(stderr,
                        "Failed to open write-file (for path: %s), error: %s\n",
                        write_path,
                        strerror(errno));
            } else {
                fprintf(stderr,
                        "Failed to open the provided write-file, error: %s\n",
                        strerror(errno));
            }
        }

        return NULL;
    }

    file = fdopen(write_fd, "w");
    if (file == NULL) {
        if (!(options & F_TBD_FOR_MAIN_IGNORE_WARNINGS)) {
            if (args->print_paths) {
                fprintf(stderr,
                        "Failed to open write-file (for path: %s) as a "
                        "file-stream, error: %s\n",
                        write_path,
                        strerror(errno));
            } else {
                fprintf(stderr,
                        "Failed to open the provided write-file as a "
                        "file-stream, error: %s\n",
                        strerror(errno));
            }
        }
    }

    if (tbd->flags & F_TBD_FOR_MAIN_COMBINE_TBDS) {
        args->combine_file = file;
    }

    *terminator_out = terminator;
    return file;
}

enum parse_macho_for_main_result
parse_macho_file_for_main(const struct parse_macho_for_main_args args) {
    if (read_magic(args.magic_in, args.magic_in_size_in, args.fd)) {
        if (errno == EOVERFLOW) {
            return E_PARSE_MACHO_FOR_MAIN_NOT_A_MACHO;
        }

        /*
         * Manually handle the read fail by passing on to
         * handle_macho_file_parse_result() as if we went to
         * macho_file_parse_from_file().
         */

        const struct handle_macho_file_parse_result_args handle_args = {
            .retained_info_in = args.retained_info_in,
            .global = args.global,
            .tbd = args.tbd,
            .dir_path = args.dir_path,
            .parse_result = E_MACHO_FILE_PARSE_READ_FAIL,
            .print_paths = args.print_paths
        };

        handle_macho_file_parse_result(handle_args);
        return E_PARSE_MACHO_FOR_MAIN_OTHER_ERROR;
    }

    /*
     * Ignore invalid fields so a mach-o file is fully parsed regardless of
     * errors. Instead, we prefer to check manually for any field errors.
     */

    const uint64_t macho_options =
        (O_MACHO_FILE_PARSE_IGNORE_INVALID_FIELDS | args.tbd->macho_options);

    struct tbd_create_info *const create_info = &args.tbd->info;
    struct tbd_create_info original_info = *create_info;

    const uint32_t magic = *(const uint32_t *)args.magic_in;
    const enum macho_file_parse_result parse_result =
        macho_file_parse_from_file(create_info,
                                   args.fd,
                                   magic,
                                   args.tbd->parse_options,
                                   macho_options);

    if (parse_result == E_MACHO_FILE_PARSE_NOT_A_MACHO) {
        if (!args.dont_handle_non_macho_error) {
            const struct handle_macho_file_parse_result_args handle_args = {
                .retained_info_in = args.retained_info_in,
                .global = args.global,
                .tbd = args.tbd,
                .dir_path = args.dir_path,
                .parse_result = parse_result,
                .print_paths = args.print_paths
            };

            handle_macho_file_parse_result(handle_args);
        }

        return E_PARSE_MACHO_FOR_MAIN_NOT_A_MACHO;
    }

    const struct handle_macho_file_parse_result_args handle_args = {
        .retained_info_in = args.retained_info_in,
        .global = args.global,
        .tbd = args.tbd,
        .dir_path = args.dir_path,
        .parse_result = parse_result,
        .print_paths = args.print_paths
    };

    const bool should_continue = handle_macho_file_parse_result(handle_args);
    if (!should_continue) {
        clear_create_info(create_info, &original_info);
        return E_PARSE_MACHO_FOR_MAIN_OTHER_ERROR;
    }

    if (args.options & O_PARSE_MACHO_FOR_MAIN_VERIFY_WRITE_PATH) {
        verify_write_path(args.tbd);
    }

    char *const write_path = args.tbd->write_path;
    const uint64_t write_path_length = args.tbd->write_path_length;

    FILE *file = NULL;
    char *terminator = NULL;

    if (write_path != NULL) {
        file = open_file_for_path(&args,
                                  write_path,
                                  write_path_length,
                                  &terminator);

        if (file == NULL) {
            clear_create_info(create_info, &original_info);
            return E_PARSE_MACHO_FOR_MAIN_OK;
        }

        tbd_for_main_write_to_file(args.tbd,
                                   write_path,
                                   write_path_length,
                                   terminator,
                                   file,
                                   args.print_paths);

        fclose(file);
    } else {
        tbd_for_main_write_to_stdout(args.tbd, args.dir_path, true);
    }

    clear_create_info(create_info, &original_info);
    return E_PARSE_MACHO_FOR_MAIN_OK;
}

enum parse_macho_for_main_result
parse_macho_file_for_main_while_recursing(
    struct parse_macho_for_main_args *__notnull const args_ptr)
{
    const struct parse_macho_for_main_args args = *args_ptr;
    if (read_magic(args.magic_in, args.magic_in_size_in, args.fd)) {
        if (errno == EOVERFLOW) {
            return E_PARSE_MACHO_FOR_MAIN_NOT_A_MACHO;
        }

        /*
         * Pass on the read-failure to
         * handle_macho_file_parse_result_while_recursing() as should be done
         * with every error faced in this function.
         */

        const struct handle_macho_file_parse_result_args handle_args = {
            .retained_info_in = args.retained_info_in,
            .global = args.global,
            .tbd = args.tbd,
            .dir_path = args.dir_path,
            .name = args.name,
            .parse_result = E_MACHO_FILE_PARSE_READ_FAIL,
            .print_paths = args.print_paths
        };

        handle_macho_file_parse_result_while_recursing(handle_args);
        return E_PARSE_MACHO_FOR_MAIN_OTHER_ERROR;
    }

    /*
     * Handle any provided replacement options.
     */

    const uint32_t magic = *(const uint32_t *)args.magic_in;

    const uint64_t parse_options = args.tbd->parse_options;
    const uint64_t macho_options =
        (O_MACHO_FILE_PARSE_IGNORE_INVALID_FIELDS | args.tbd->macho_options);

    struct tbd_create_info *const create_info = &args.tbd->info;
    struct tbd_create_info original_info = *create_info;

    const enum macho_file_parse_result parse_result =
        macho_file_parse_from_file(create_info,
                                   args.fd,
                                   magic,
                                   parse_options,
                                   macho_options);

    if (parse_result == E_MACHO_FILE_PARSE_NOT_A_MACHO) {
        if (!args.dont_handle_non_macho_error) {
            const struct handle_macho_file_parse_result_args handle_args = {
                .retained_info_in = args.retained_info_in,
                .global = args.global,
                .tbd = args.tbd,
                .dir_path = args.dir_path,
                .name = args.name,
                .parse_result = parse_result,
                .print_paths = args.print_paths
            };

            handle_macho_file_parse_result_while_recursing(handle_args);
        }

        return E_PARSE_MACHO_FOR_MAIN_NOT_A_MACHO;
    }

    const struct handle_macho_file_parse_result_args handle_args = {
        .retained_info_in = args.retained_info_in,
        .global = args.global,
        .tbd = args.tbd,
        .dir_path = args.dir_path,
        .name = args.name,
        .parse_result = parse_result,
        .print_paths = args.print_paths
    };

    const bool should_continue =
        handle_macho_file_parse_result_while_recursing(handle_args);

    if (!should_continue) {
        clear_create_info(create_info, &original_info);
        return E_PARSE_MACHO_FOR_MAIN_OTHER_ERROR;
    }

    uint64_t write_path_length = 0;

    char *write_path = NULL;
    const bool should_combine =
        (args.tbd->flags & F_TBD_FOR_MAIN_COMBINE_TBDS);

    if (!should_combine) {
        write_path =
            tbd_for_main_create_write_path_for_recursing(args.tbd,
                                                         args.dir_path,
                                                         args.dir_path_length,
                                                         args.name,
                                                         args.name_length,
                                                         "tbd",
                                                         3,
                                                         &write_path_length);

        if (write_path == NULL) {
            fputs("Failed to allocate memory\n", stderr);
            exit(1);
        }
    } else {
        write_path = args.tbd->write_path;
        write_path_length = args.tbd->write_path_length;

        args.tbd->write_options |= O_TBD_CREATE_IGNORE_FOOTER;
    }

    char *terminator = NULL;
    FILE *const file =
        open_file_for_path_while_recursing(args_ptr,
                                           write_path,
                                           write_path_length,
                                           &terminator);

    if (file == NULL) {
        if (!should_combine) {
            free(write_path);
        }
        
        clear_create_info(create_info, &original_info);
        return E_PARSE_MACHO_FOR_MAIN_OTHER_ERROR;
    }

    tbd_for_main_write_to_file(args.tbd,
                               write_path,
                               write_path_length,
                               terminator,
                               file,
                               args.print_paths);

    if (!should_combine) {
        fclose(file);
        free(write_path);
    }

    clear_create_info(create_info, &original_info);
    return E_PARSE_MACHO_FOR_MAIN_OK;
}
