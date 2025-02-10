// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "strhashtbl.h"
#include <string.h>
#include <stdio.h>

typedef struct str_hash_tbl_node
{
    struct str_hash_tbl_node* next;
    struct str_hash_tbl_node* prev;
    uint64_t code;
    char* key;
    void* value;
}
str_hash_tbl_node_t;

static uint64_t _hash(const char* key)
{
#if 0
    size_t n = strlen(key);

    if (n == 0)
        return 0;

    const uint64_t first = key[0];
    const uint64_t middle = key[n/2];
    const uint64_t last = key[n-1];

    return (first << 16) | (middle << 8) | last;
#else
    const char* p = key;
    uint64_t code = 0;

    while (*p)
    {
        uint64_t x = *p++;
        code += x;
    }

    return code;
#endif
}

void str_hash_tbl_init(str_hash_tbl_t* tbl)
{
    memset(tbl, 0, sizeof(str_hash_tbl_t));
}

int str_hash_tbl_insert(str_hash_tbl_t* tbl, const char* key, void* value)
{
    int ret = -1;
    uint64_t code;
    uint64_t index;
    str_hash_tbl_node_t* p;

    if (!tbl || !key)
        goto done;

    code = _hash(key);
    index = code % STR_HASH_TBL_MAX_CHAINS;

    /* check whether key already in this chain */
    for (p = tbl->chains[index]; p; p = p->next)
    {
        if (p->code == code)
        {
            if (strcmp(p->key, key) == 0)
                goto done;
        }
    }

    /* allocate new node */
    if (!(p = calloc(1, sizeof(str_hash_tbl_node_t))))
        goto done;

    /* make copy of the string */
    if (!(p->key = strdup(key)))
    {
        free(p);
        goto done;
    }

    p->code = code;
    p->value = value;

    if (tbl->chains[index])
    {
        tbl->chains[index]->prev = p;
        p->next = tbl->chains[index];
        tbl->chains[index] = p;
    }
    else
    {
        tbl->chains[index] = p;
        p->next = NULL;
        p->prev = NULL;
    }

    tbl->size++;

    ret = 0;

done:

    return ret;
}

int str_hash_tbl_find(const str_hash_tbl_t* tbl, const char* key, void** value)
{
    int ret = -1;
    uint64_t code;
    uint64_t index;
    str_hash_tbl_node_t* p;

    if (!tbl || !key || !value)
        goto done;

    *value = NULL;

    code = _hash(key);
    index = code % STR_HASH_TBL_MAX_CHAINS;

    /* check whether key already in this chain */
    for (p = tbl->chains[index]; p; p = p->next)
    {
        if (p->code == code && strcmp(p->key, key) == 0)
        {
            *value = p->value;
            ret = 0;
            goto done;
        }
    }

    /* not found */
    ret = -1;

done:

    return ret;
}

int str_hash_tbl_release(str_hash_tbl_t* tbl, void (*dealloc)(void* value))
{
    int ret = -1;

    if (!tbl)
        goto done;

    for (size_t i = 0; i < STR_HASH_TBL_MAX_CHAINS; i++)
    {
        str_hash_tbl_node_t* p = tbl->chains[i];

        while (p)
        {
            str_hash_tbl_node_t* next = p->next;

            free(p->key);
            if (dealloc)
                (*dealloc)(p->value);
            free(p);

            p = next;
        }
    }

    memset(tbl, 0, sizeof(str_hash_tbl_t));

    ret = 0;

done:

    return ret;
}
