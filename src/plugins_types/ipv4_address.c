/**
 * @file ipv4_address.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief ietf-inet-types ipv4-address type plugin.
 *
 * Copyright (c) 2019-2021 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#define _GNU_SOURCE /* asprintf, strdup */
#include <sys/cdefs.h>

#include "plugins_types.h"

#include <arpa/inet.h>
#if defined (__FreeBSD__) || defined (__NetBSD__) || defined (__OpenBSD__)
#include <netinet/in.h>
#include <sys/socket.h>
#endif
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "libyang.h"

#include "common.h"
#include "compat.h"

/**
 * @page howtoDataLYB LYB Binary Format
 * @subsection howtoDataLYBTypesIPv4Address ipv4-address (ietf-inet-types)
 *
 * | Size (B) | Mandatory | Type | Meaning |
 * | :------  | :-------: | :--: | :-----: |
 * | 4       | yes       | `struct in_addr *` | IPv4 address in network-byte order |
 * | string length | no        | `char *` | IPv4 address zone string |
 */

/**
 * @brief Stored value structure for ipv4-address
 */
struct lyd_value_ipv4_address {
    struct in_addr addr;
    const char *zone;
};

static void lyplg_type_free_ipv4_address(const struct ly_ctx *ctx, struct lyd_value *value);

/**
 * @brief Convert IP address with optional zone to network-byte order.
 *
 * @param[in] value Value to convert.
 * @param[in] value_len Length of @p value.
 * @param[in] options Type store callback options.
 * @param[in] ctx libyang context with dictionary.
 * @param[in,out] addr Allocated value for the address.
 * @param[out] zone Ipv6 address zone in dictionary.
 * @param[out] err Error information on error.
 * @return LY_ERR value.
 */
static LY_ERR
ipv4address_str2ip(const char *value, size_t value_len, uint32_t options, const struct ly_ctx *ctx,
        struct in_addr *addr, const char **zone, struct ly_err_item **err)
{
    LY_ERR ret = LY_SUCCESS;
    const char *addr_no_zone;
    char *zone_ptr = NULL, *addr_dyn = NULL;
    size_t zone_len;

    /* store zone and get the string IPv4 address without it */
    if ((zone_ptr = ly_strnchr(value, '%', value_len))) {
        /* there is a zone index */
        zone_len = value_len - (zone_ptr - value) - 1;
        ret = lydict_insert(ctx, zone_ptr + 1, zone_len, zone);
        LY_CHECK_GOTO(ret, cleanup);

        /* get the IP without it */
        if (options & LYPLG_TYPE_STORE_DYNAMIC) {
            *zone_ptr = '\0';
            addr_no_zone = value;
        } else {
            addr_dyn = strndup(value, zone_ptr - value);
            addr_no_zone = addr_dyn;
        }
    } else {
        /* no zone */
        *zone = NULL;

        /* get the IP terminated with zero */
        if (options & LYPLG_TYPE_STORE_DYNAMIC) {
            /* we can use the value directly */
            addr_no_zone = value;
        } else {
            addr_dyn = strndup(value, value_len);
            addr_no_zone = addr_dyn;
        }
    }

    /* store the IPv4 address in network-byte order */
    if (!inet_pton(AF_INET, addr_no_zone, addr)) {
        ret = ly_err_new(err, LY_EVALID, LYVE_DATA, NULL, NULL, "Failed to convert IPv4 address \"%s\".", addr_no_zone);
        goto cleanup;
    }

    /* restore the value */
    if ((options & LYPLG_TYPE_STORE_DYNAMIC) && zone_ptr) {
        *zone_ptr = '%';
    }

cleanup:
    free(addr_dyn);
    return ret;
}

/**
 * @brief Implementation of ::lyplg_type_store_clb for the ipv4-address ietf-inet-types type.
 */
static LY_ERR
lyplg_type_store_ipv4_address(const struct ly_ctx *ctx, const struct lysc_type *type, const void *value, size_t value_len,
        uint32_t options, LY_VALUE_FORMAT format, void *UNUSED(prefix_data), uint32_t hints,
        const struct lysc_node *UNUSED(ctx_node), struct lyd_value *storage, struct lys_glob_unres *UNUSED(unres),
        struct ly_err_item **err)
{
    LY_ERR ret = LY_SUCCESS;
    const char *value_str = value;
    struct lysc_type_str *type_str = (struct lysc_type_str *)type;
    struct lyd_value_ipv4_address *val;
    size_t i;

    /* zero storage so we can always free it */
    memset(storage, 0, sizeof *storage);

    if (format == LY_VALUE_LYB) {
        /* validation */
        if (value_len < 4) {
            ret = ly_err_new(err, LY_EVALID, LYVE_DATA, NULL, NULL, "Invalid LYB ipv4-address value size %zu "
                    "(expected at least 4).", value_len);
            goto cleanup;
        }
        for (i = 4; i < value_len; ++i) {
            if (!isalnum(value_str[i])) {
                ret = ly_err_new(err, LY_EVALID, LYVE_DATA, NULL, NULL, "Invalid LYB ipv4-address zone character 0x%x.",
                        value_str[i]);
                goto cleanup;
            }
        }

        /* allocate the value */
        val = malloc(sizeof *val);
        LY_CHECK_ERR_GOTO(!val, ret = LY_EMEM, cleanup);

        /* init storage */
        storage->_canonical = NULL;
        storage->ptr = val;
        storage->realtype = type;

        /* store IP address */
        memcpy(&val->addr, value, sizeof val->addr);

        /* store zone, if any */
        if (value_len > 4) {
            ret = lydict_insert(ctx, value + 4, value_len - 4, &val->zone);
            LY_CHECK_GOTO(ret, cleanup);
        } else {
            val->zone = NULL;
        }

        /* success */
        goto cleanup;
    }

    /* check hints */
    ret = lyplg_type_check_hints(hints, value, value_len, type->basetype, NULL, err);
    LY_CHECK_GOTO(ret, cleanup);

    /* length restriction of the string */
    if (type_str->length) {
        /* value_len is in bytes, but we need number of characters here */
        ret = lyplg_type_validate_range(LY_TYPE_STRING, type_str->length, ly_utf8len(value, value_len), value, value_len, err);
        LY_CHECK_GOTO(ret, cleanup);
    }

    /* pattern restrictions */
    ret = lyplg_type_validate_patterns(type_str->patterns, value, value_len, err);
    LY_CHECK_GOTO(ret, cleanup);

    /* allocate the value */
    val = calloc(1, sizeof *val);
    LY_CHECK_ERR_GOTO(!val, ret = LY_EMEM, cleanup);

    /* init storage */
    storage->_canonical = NULL;
    storage->ptr = val;
    storage->realtype = type;

    /* get the network-byte order address */
    ret = ipv4address_str2ip(value, value_len, options, ctx, &val->addr, &val->zone, err);
    LY_CHECK_GOTO(ret, cleanup);

    /* store canonical value */
    if (options & LYPLG_TYPE_STORE_DYNAMIC) {
        ret = lydict_insert_zc(ctx, (char *)value, &storage->_canonical);
        options &= ~LYPLG_TYPE_STORE_DYNAMIC;
        LY_CHECK_GOTO(ret, cleanup);
    } else {
        ret = lydict_insert(ctx, value_len ? value : "", value_len, &storage->_canonical);
        LY_CHECK_GOTO(ret, cleanup);
    }

cleanup:
    if (options & LYPLG_TYPE_STORE_DYNAMIC) {
        free((void *)value);
    }

    if (ret) {
        lyplg_type_free_ipv4_address(ctx, storage);
    }
    return ret;
}

/**
 * @brief Implementation of ::lyplg_type_compare_clb for the ipv4-address ietf-inet-types type.
 */
static LY_ERR
lyplg_type_compare_ipv4_address(const struct lyd_value *val1, const struct lyd_value *val2)
{
    struct lyd_value_ipv4_address *v1 = val1->ptr, *v2 = val2->ptr;

    if (val1->realtype != val2->realtype) {
        return LY_ENOT;
    }

    /* zones are NULL or in the dictionary */
    if (memcmp(&v1->addr, &v2->addr, sizeof v1->addr) || (v1->zone != v2->zone)) {
        return LY_ENOT;
    }
    return LY_SUCCESS;
}

/**
 * @brief Implementation of ::lyplg_type_print_clb for the ipv4-address ietf-inet-types type.
 */
static const void *
lyplg_type_print_ipv4_address(const struct ly_ctx *ctx, const struct lyd_value *value, LY_VALUE_FORMAT format,
        void *UNUSED(prefix_data), ly_bool *dynamic, size_t *value_len)
{
    struct lyd_value_ipv4_address *val = value->ptr;
    size_t zone_len;
    char *ret;

    if (format == LY_VALUE_LYB) {
        if (!val->zone) {
            /* address-only, const */
            *dynamic = 0;
            if (value_len) {
                *value_len = sizeof val->addr;
            }
            return &val->addr;
        }

        /* dynamic */
        zone_len = strlen(val->zone);
        ret = malloc(sizeof val->addr + zone_len);
        LY_CHECK_RET(!ret, NULL);

        memcpy(ret, &val->addr, sizeof val->addr);
        memcpy(ret + sizeof val->addr, val->zone, zone_len);

        *dynamic = 1;
        if (value_len) {
            *value_len = sizeof val->addr + zone_len;
        }
        return ret;
    }

    /* generate canonical value if not already */
    if (!value->_canonical) {
        /* '%' + zone */
        zone_len = val->zone ? strlen(val->zone) + 1 : 0;
        ret = malloc(INET_ADDRSTRLEN + zone_len);
        LY_CHECK_RET(!ret, NULL);

        /* get the address in string */
        if (!inet_ntop(AF_INET, &val->addr, ret, INET_ADDRSTRLEN)) {
            free(ret);
            LOGERR(ctx, LY_EVALID, "Failed to get IPv4 address in string (%s).", strerror(errno));
            return NULL;
        }

        /* add zone */
        if (zone_len) {
            sprintf(ret + strlen(ret), "%%%s", val->zone);
        }

        /* store it */
        if (lydict_insert_zc(ctx, ret, (const char **)&value->_canonical)) {
            LOGMEM(ctx);
            return NULL;
        }
    }

    /* use the cached canonical value */
    if (dynamic) {
        *dynamic = 0;
    }
    if (value_len) {
        *value_len = strlen(value->_canonical);
    }
    return value->_canonical;
}

/**
 * @brief Implementation of ::lyplg_type_hash_clb for the ipv4-address ietf-inet-types type.
 */
static const void *
lyplg_type_hash_ipv4_address(const struct lyd_value *value, ly_bool *dynamic, size_t *key_len)
{
    /* simply use the (dynamic or const) LYB value */
    return lyplg_type_print_ipv4_address(NULL, value, LY_VALUE_LYB, NULL, dynamic, key_len);
}

/**
 * @brief Implementation of ::lyplg_type_dup_clb for the ipv4-address ietf-inet-types type.
 */
static LY_ERR
lyplg_type_dup_ipv4_address(const struct ly_ctx *ctx, const struct lyd_value *original, struct lyd_value *dup)
{
    LY_ERR ret;
    struct lyd_value_ipv4_address *orig_val = original->ptr, *dup_val;

    ret = lydict_insert(ctx, original->_canonical, ly_strlen(original->_canonical), &dup->_canonical);
    LY_CHECK_RET(ret);

    dup_val = malloc(sizeof *dup_val);
    if (!dup_val) {
        lydict_remove(ctx, dup->_canonical);
        return LY_EMEM;
    }
    memcpy(&dup_val->addr, &orig_val->addr, sizeof orig_val->addr);
    ret = lydict_insert(ctx, orig_val->zone, 0, &dup_val->zone);
    if (ret) {
        lydict_remove(ctx, dup->_canonical);
        free(dup_val);
        return ret;
    }

    dup->ptr = dup_val;
    dup->realtype = original->realtype;
    return LY_SUCCESS;
}

/**
 * @brief Implementation of ::lyplg_type_free_clb for the ipv4-address ietf-inet-types type.
 */
static void
lyplg_type_free_ipv4_address(const struct ly_ctx *ctx, struct lyd_value *value)
{
    struct lyd_value_ipv4_address *val = value->ptr;

    lydict_remove(ctx, value->_canonical);
    if (val) {
        lydict_remove(ctx, val->zone);
        free(val);
    }
}

/**
 * @brief Plugin information for ipv4-address type implementation.
 *
 * Note that external plugins are supposed to use:
 *
 *   LYPLG_TYPES = {
 */
const struct lyplg_type_record plugins_ipv4_address[] = {
    {
        .module = "ietf-inet-types",
        .revision = "2013-07-15",
        .name = "ipv4-address",

        .plugin.id = "libyang 2 - ipv4-address, version 1",
        .plugin.store = lyplg_type_store_ipv4_address,
        .plugin.validate = NULL,
        .plugin.compare = lyplg_type_compare_ipv4_address,
        .plugin.print = lyplg_type_print_ipv4_address,
        .plugin.hash = lyplg_type_hash_ipv4_address,
        .plugin.duplicate = lyplg_type_dup_ipv4_address,
        .plugin.free = lyplg_type_free_ipv4_address
    },
    {0}
};
