// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "conf.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <efi.h>
#include <efilib.h>
#include "strings.h"
#include "allocator.h"

static const char* _get_line(const char** pp, const char* end)
{
    const char* p = *pp;
    const char* start = p;

    if (p == end)
        return NULL;

    while (p != end && *p++ != '\n')
        ;

    *pp = p;

    return start;
}

static const char* _skip_ident(const char* p, const char* end)
{
    if (p == end)
        return p;

    if (!isalpha(*p) || *p == '_')
        return p;

    p++;

    while (p != end && (isalnum(*p) || *p == '_'))
        p++;

    return p;
}

static const char* _skip_whitespace(const char* p, const char* end)
{
    while (p != end && isspace(*p))
        p++;

    return p;
}

int conf_parse(
    const char* text,
    unsigned long text_size,
    conf_callback_t callback,
    void* callback_data,
    unsigned int* error_line,
    err_t* err)
{
    int status = 0;
    const char* line;
    const char* text_end;
    unsigned int lineNum = 0;
    char* name_ptr = NULL;
    char* value_ptr = NULL;

    /* Check parameters */
    if (!text || !error_line || !err)
    {
        err_format(err, "invalid parameter");
        status = -1;
        goto done;
    }

    /* Set pointer to the end of the text */
    text_end = text + text_size;

    /* Clear error state */
    *error_line = 0;
    err_clear(err);

    /* Process lines of the format NAME=SHA1:SHA256 */
    while ((line = _get_line(&text, text_end)))
    {
        const char* p = line;
        const char* end = text;
        const char* name = NULL;
        UINTN name_len = 0;
        const char* value = NULL;
        UINTN value_len = 0;

        /* Increment the line number */
        lineNum++;

        /* Strip horizontal whitespace */
        p = _skip_whitespace(p, end);

        /* Skip blank lines and comment lines */
        if (p == end || *p == '#')
            continue;

        /* Remove trailing whitespace */
        while (end != p && isspace(end[-1]))
            end--;

        /* Recognize the name: [A-Za-z_][A-Za-z_0-9] */
        {
            const char* start = p;

            p = _skip_ident(p, end);

            if (p == start)
            {
                err_format(err, "expected name");
                status = -1;
                goto done;
            }

            /* Save the name */
            name = start;
            name_len = p - start;
        }

        /* Expect a '=' */
        {
            p = _skip_whitespace(p, end);

            if (p == end || *p++ != '=')
            {
                err_format(err, "syntax error: expected '='");
                status = -1;
                goto done;
            }

            p = _skip_whitespace(p, end);
        }

        /* Get the value */
        {
            value = p;
            value_len = end - p;
        }

        /* Invoke the callback */
        if (callback)
        {
            if (!(name_ptr = __allocator.alloc(name_len+1)))
            {
                err_format(err, "out of memory");
                status = -1;
                goto done;
            }

            if (!(value_ptr = __allocator.alloc(value_len+1)))
            {
                err_format(err, "out of memory");
                status = -1;
                goto done;
            }

            memcpy(name_ptr, name, name_len);
            name_ptr[name_len] = '\0';
            memcpy(value_ptr, value, value_len);
            value_ptr[value_len] = '\0';

            if ((*callback)(name_ptr, value_ptr, callback_data, err) != 0)
            {
                status = -1;
                goto done;
            }

            __allocator.free(name_ptr);
            name_ptr = NULL;
            __allocator.free(value_ptr);
            value_ptr = NULL;
        }
    }

done:

    if (name_ptr)
        __allocator.free(name_ptr);

    if (value_ptr)
        __allocator.free(value_ptr);

    if (status != 0)
        *error_line = lineNum;

    return status;
}
