// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "events.h"
#include "strings.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "allocator.h"
#include "json.h"
#include "sha256.h"
#include "sig.h"

#define TYPE_SIZE 16

#define OS_IMAGE_IDENTITY_FORMAT "\"os-image-identity\":{\"signer\":\"%s\",\"svn\":\"%s\",\"diskId\":\"%s\",\"eventVersion\":\"%s\"}"

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

/* examples: "PCR1", "PCR23" */
static int _parse_pcr(const char** ptr, uint32_t* pcrnum_out, err_t* err)
{
    int ret = -1;
    const char pcr[] = "PCR";
    const size_t pcrlen = sizeof(pcr) - 1;
    char digits[3];
    uint32_t pcrnum;
    const char* p = *ptr;

    *pcrnum_out = 0;

    /* Parse the "PCR" string */
    {
        if (strncmp(p, "PCR", pcrlen) != 0)
        {
            err_format(err, "expected PCR");
            goto done;
        }

        p += pcrlen;
    }

    /* Extract the PCR number */
    {
        size_t len;
        const char* start = p;

        /* skip over digits */
        while (isdigit(*p))
            p++;

        /* get number of digits */
        len = p - start;

        /* Accept at most 2 digits */
        if (!(len >= 1 && len <= 2))
        {
            err_format(err, "too little or too many PCR digits");
            goto done;
        }

        *digits = '\0';
        strncat(digits, start, len);
    }

    /* Convert PCR number to integer */
    if (str2u32(digits, &pcrnum) < 0)
    {
        err_format(err, "str2u32() unexpected failure");
        goto done;
    }

    /* Reject out of range PCR numbers */
    if (pcrnum < 0 || pcrnum > 23)
    {
        err_format(err, "PCR number is out of range");
        goto done;
    }

    *pcrnum_out = pcrnum;
    *ptr = p;
    ret = 0;

done:
    return ret;
}

/* examples: "string", "binary" */
static int _parse_type(const char** ptr, char type[TYPE_SIZE], err_t* err)
{
    int ret = -1;
    const char* p = *ptr;

    *type = '\0';

    if (strncmp(p, "string", 6) == 0)
    {
        strlcpy(type, "string", TYPE_SIZE);
        p += 6;
    }
    else if (strncmp(p, "binary", 6) == 0)
    {
        strlcpy(type, "binary", TYPE_SIZE);
        p += 6;
    }
    else
    {
        err_format(err, "unknown type");
        goto done;
    }

    *ptr = p;
    ret = 0;

done:
    return ret;
}

static int _format_os_image_identity_json(
    char* buf,
    size_t size,
    const char* signer,
    const identity_t* identity)
{
    int ret = -1;
    int n;

    if (!identity->svn || !identity->diskId || !identity->eventVersion)
        goto done;

    n = snprintf(
        buf,
        size,
        OS_IMAGE_IDENTITY_FORMAT,
        signer,
        identity->svn,
        identity->diskId,
        identity->eventVersion);

    if (n >= size)
        goto done;

    ret = 0;

done:
    return ret;
}

static int _parse_os_image_identity(const char* text, identity_t* id);

static int _dispatch_callback(
    const char* p,
    size_t len,
    size_t index,
    uint32_t pcrnum,
    const char* type,
    const char* signer,
    bool* found_os_image_identity,
    process_events_callback_t callback,
    void* callback_data,
    err_t* err)
{
    int ret = -1;
    char* data = NULL;
    const char os_image_identity[] = "\"os-image-identity\":";

    /* Make a zero-terminated copy of p buffer */
    {
        if (!(data = __allocator.alloc(len + 1)))
        {
            err_format(err, "out of memory");
            goto done;
        }

        memcpy(data, p, len);
        data[len] = '\0';
    }

    /* Reformat "os-image-identity" JSON element, adding "signer" field  */
    if (strcmp(type, "string") == 0 &&
        strncmp(data, os_image_identity, sizeof(os_image_identity)-1) == 0)
    {
        void* buf;
        size_t buf_size = 0;
        identity_t id;

        if (_parse_os_image_identity(data, &id) < 0)
        {
            err_format(err, "failed to parse os-image-identity");
            goto done;
        }

        /* calculate buffer size: add value lengths, subtract "%s" lengths */
        buf_size += strlen(OS_IMAGE_IDENTITY_FORMAT);
        buf_size += strlen(signer) - 2; /* subtract 2 for "%s" */
        buf_size += strlen(id.svn) - 2; /* subtract 2 for "%s" */
        buf_size += strlen(id.diskId) - 2; /* subtract 2 for "%s" */
        buf_size += strlen(id.eventVersion) - 2; /* subtract 2 for "%s" */
        buf_size += 1;

        if (!(buf = __allocator.alloc(buf_size)))
        {
            err_format(err, "out of memory");
            goto done;
        }

        memset(buf, 0, buf_size);

        if (_format_os_image_identity_json(buf, buf_size, signer, &id) < 0)
        {
            __allocator.free(buf);
            err_format(err, "failed to format os-image-identity");
            goto done;
        }

        *found_os_image_identity = true;
        __allocator.free(data);
        data = buf;
    }

    if ((*callback)(index, pcrnum, type, data, signer, callback_data) < 0)
    {
        err_format(err,
            "process_events_callback_t failed: pcr=%d type=%s data=%s",
            pcrnum, type, data);
        goto done;
    }

    ret = 0;

done:

    if (data)
        __allocator.free(data);

    return ret;
}

int parse_events_file(
    const char* text,
    unsigned long text_size,
    const char* signer,
    process_events_callback_t callback,
    void* callback_data,
    unsigned int* error_line,
    err_t* err)
{
    int status = -1;
    const char* line;
    const char* text_end;
    unsigned int line_num = 0;
    const char* ptr;
    size_t index = 0;
    bool found_os_image_identity = false;

    /* Check parameters */
    if (!text || !text_size || !error_line || !err)
    {
        err_format(err, "invalid parameter");
        goto done;
    }

    /* Set pointer to the end of the text */
    text_end = text + text_size;

    /* Clear error state */
    *error_line = 0;
    err_clear(err);

    /* Set pointer to text */
    ptr = text;

    /* Process lines of the format PCR<NUM>:DATA */
    while ((line = _get_line(&ptr, text_end)))
    {
        const char* p = line;
        const char* end = ptr;
        uint32_t pcrnum = 0;
        char type[TYPE_SIZE];

        /* Increment the line number */
        line_num++;

        /* Strip horizontal whitespace */
        while (p != end && isspace(*p))
            p++;

        /* Skip blank lines and comment lines */
        if (p == end || *p == '#')
            continue;

        /* Remove trailing whitespace */
        while (end != p && isspace(end[-1]))
            end--;

        /* Parse the PCR field */
        if (_parse_pcr(&p, &pcrnum, err) < 0)
            goto done;

        /* expect a colon separator */
        if (*p++ != ':')
        {
            err_format(err, "expected colon character");
            goto done;
        }

        /* Parse the type field */
        if (_parse_type(&p, type, err) < 0)
            goto done;

        /* expect a colon separator */
        if (*p++ != ':')
        {
            err_format(err, "expected colon character");
            goto done;
        }

        /* extend the PCR and log */
        if (_dispatch_callback(
            p,
            end - p,
            index,
            pcrnum,
            type,
            signer,
            &found_os_image_identity,
            callback,
            callback_data,
            err) < 0)
        {
            goto done;
        }

        index++;
    }

    if (!found_os_image_identity)
    {
        err_format(err, "required os-image-identity element not found");
        goto done;
    }

    status = 0;

done:

    if (status != 0 && error_line)
        *error_line = line_num;

    return status;
}

typedef struct _json_callback_data
{
    identity_t id;
}
json_callback_data_t;

static json_result_t _json_parser_callback(
    json_parser_t* parser,
    json_reason_t reason,
    json_type_t type,
    const json_union_t* value,
    void* callback_data)
{
    json_result_t result = JSON_UNEXPECTED;
    json_callback_data_t* cbd = (json_callback_data_t*)callback_data;

    switch (reason)
    {
        case JSON_REASON_NONE:
        {
            break;
        }
        case JSON_REASON_NAME:
        {
            break;
        }
        case JSON_REASON_BEGIN_OBJECT:
        {
            break;
        }
        case JSON_REASON_END_OBJECT:
        {
            break;
        }
        case JSON_REASON_BEGIN_ARRAY:
        {
            break;
        }
        case JSON_REASON_END_ARRAY:
        {
            break;
        }
        case JSON_REASON_VALUE:
        {
            if (json_match(parser, "os-image-identity.signer") == JSON_OK)
            {
                // Ignore the signer and override it with image signer
            }
            else if (json_match(parser, "os-image-identity.svn") == JSON_OK)
            {
                if (cbd->id.svn)
                    goto done;

                if (!(cbd->id.svn = strdup(value->string)))
                    goto done;
            }
            else if (json_match(parser, "os-image-identity.diskId") == JSON_OK)
            {
                if (cbd->id.diskId)
                    goto done;

                if (!(cbd->id.diskId = strdup(value->string)))
                    goto done;
            }
            else if (json_match(
                parser, "os-image-identity.eventVersion") == JSON_OK)
            {
                if (cbd->id.eventVersion)
                    goto done;

                if (!(cbd->id.eventVersion = strdup(value->string)))
                    goto done;
            }
            else
            {
                result = JSON_UNKNOWN_VALUE;
                goto done;
            }
            break;
        }
    }

    result = JSON_OK;

done:
    return result;
}

static void* _alloc(size_t size)
{
    return __allocator.alloc(size);
}

static void _free(void* ptr)
{
    __allocator.free(ptr);
}

static json_allocator_t _json_allocator = { _alloc, _free };

static int _parse_os_image_identity(const char* text, identity_t* id)
{
    int ret = -1;
    json_parser_t parser;
    json_result_t r;
    const json_parser_options_t options = {1};
    size_t text_size = strlen(text) + 1;
    char* str = NULL;
    size_t str_size = text_size + 2;
    json_callback_data_t cbd;

    memset(id, 0, sizeof(identity_t));
    memset(&cbd, 0, sizeof(cbd));

    if (!(str = __allocator.alloc(str_size)))
        goto done;

    strlcpy3(str, "{", text, "}", str_size);

    if ((r = json_parser_init(
        &parser,
        str,
        str_size,
        _json_parser_callback,
        &cbd,
        &_json_allocator,
        &options) != JSON_OK))
    {
        goto done;
    }

    if ((r = json_parser_parse(&parser)) != JSON_OK)
        goto done;

    if (parser.depth != 0)
        goto done;

    /* all fields must be present */
    if (!cbd.id.svn || !cbd.id.diskId || !cbd.id.eventVersion)
        goto done;

    *id = cbd.id;

    ret = 0;

done:

    if (str)
        __allocator.free(str);

    return ret;
}
