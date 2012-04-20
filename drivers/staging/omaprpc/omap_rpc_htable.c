/*
 * OMAP Remote Procedure Call Driver.
 *
 * Copyright(c) 2011 Texas Instruments. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name Texas Instruments nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "omap_rpc_internal.h"

#if defined(OMAPRPC_USE_HASH)
uint32_t htable_hash(addr_htable_t *aht, long key)
{
    uint32_t mask = (1 << aht->pow) - 1;
    key += (key << 10);
    key ^= (key >> 6);
    key += (key << 3);
    key ^= (key >> 11);
    key += (key << 15);
    return (uint32_t)key&mask;      /* creates a power of two range mask */
}

int htable_get(addr_htable_t *aht, addr_record_t *ar)
{
    if (aht && aht->lines)
    {
        /* find the index */
        uint32_t index = htable_hash(aht, ar->handle);
        if (!list_empty(&aht->lines[index]))
        {
            struct list_head *pos = NULL;
            list_for_each(pos, &aht->lines[index])
            {
                addr_record_t *art = (addr_record_t *)pos;
                if (art->handle == ar->handle)
                {
                    memcpy(ar, art, sizeof(addr_record_t));
                    return 1;
                }
            }
        }
    }
    return 0;
}

int htable_set(addr_htable_t *aht, addr_record_t *ar)
{
    if (aht && aht->lines)
    {
        /* find the index */
        uint32_t index = htable_hash(aht, ar->handle);

        /* is it already there? */
        if (list_empty(&aht->lines[index]))
        {
            /* add it to the list */
            list_add((struct list_head *)ar, &aht->lines[index]);
            return 1;
        }
        else
        {
            struct list_head *pos = NULL;
            int found = 0;
            /* search for an existing version */
            list_for_each(pos, &aht->lines[index])
            {
                addr_record_t *art = (addr_record_t *)pos;
                /* if keys match */
                if (art->handle == ar->handle)
                {
                    if (art->uva == ar->uva &&
                        art->lpa == ar->lpa &&
                        art->rpa == ar->rpa)
                    {
                        /* already set, ignore. */
                        return 1;
                    }
                    else
                    {
                        printk("OMAPRPC HTABLE: WARNING! HANDLE COLLISION!\n");
                        aht->collisions++;
                        return 0;
                    }
                }
            }

            if (found == 0)
            {
                list_add((struct list_head *)ar, &aht->lines[index]);
                return 1;
            }
        }
    }
    return 0;
}
#endif

