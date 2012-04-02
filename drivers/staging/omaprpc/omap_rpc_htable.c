
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

