/*
 * datatypes.h: management of structs for public data types
 *
 * Copyright (C) 2006-2008 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 *
 */

#include <config.h>

#include "datatypes.h"
#include "virterror_internal.h"
#include "logging.h"
#include "memory.h"

/************************************************************************
 *									*
 *			Domain and Connections allocations		*
 *									*
 ************************************************************************/
/**
 * virLibConnError:
 * @conn: the connection if available
 * @error: the error number
 * @info: extra information string
 *
 * Handle an error at the connection level
 */
static void
virLibConnError(virConnectPtr conn, virErrorNumber error, const char *info)
{
    const char *errmsg;

    if (error == VIR_ERR_OK)
        return;

    errmsg = virErrorMsg(error, info);
    virRaiseError(conn, NULL, NULL, VIR_FROM_NONE, error, VIR_ERR_ERROR,
                  errmsg, info, NULL, 0, 0, errmsg, info);
}

/**
 * virDomainFreeName:
 * @domain: a domain object
 *
 * Destroy the domain object, this is just used by the domain hash callback.
 *
 * Returns 0 in case of success and -1 in case of failure.
 */
static int
virDomainFreeName(virDomainPtr domain, const char *name ATTRIBUTE_UNUSED)
{
    return (virDomainFree(domain));
}

/**
 * virNetworkFreeName:
 * @network: a network object
 *
 * Destroy the network object, this is just used by the network hash callback.
 *
 * Returns 0 in case of success and -1 in case of failure.
 */
static int
virNetworkFreeName(virNetworkPtr network, const char *name ATTRIBUTE_UNUSED)
{
    return (virNetworkFree(network));
}

/**
 * virStoragePoolFreeName:
 * @pool: a pool object
 *
 * Destroy the pool object, this is just used by the pool hash callback.
 *
 * Returns 0 in case of success and -1 in case of failure.
 */
static int
virStoragePoolFreeName(virStoragePoolPtr pool, const char *name ATTRIBUTE_UNUSED)
{
    return (virStoragePoolFree(pool));
}

/**
 * virStorageVolFreeName:
 * @vol: a vol object
 *
 * Destroy the vol object, this is just used by the vol hash callback.
 *
 * Returns 0 in case of success and -1 in case of failure.
 */
static int
virStorageVolFreeName(virStorageVolPtr vol, const char *name ATTRIBUTE_UNUSED)
{
    return (virStorageVolFree(vol));
}

/**
 * virGetConnect:
 *
 * Allocates a new hypervisor connection structure
 *
 * Returns a new pointer or NULL in case of error.
 */
virConnectPtr
virGetConnect(void) {
    virConnectPtr ret;

    if (VIR_ALLOC(ret) < 0) {
        virLibConnError(NULL, VIR_ERR_NO_MEMORY, _("allocating connection"));
        goto failed;
    }
    ret->magic = VIR_CONNECT_MAGIC;
    ret->driver = NULL;
    ret->networkDriver = NULL;
    ret->privateData = NULL;
    ret->networkPrivateData = NULL;
    ret->domains = virHashCreate(20);
    if (ret->domains == NULL)
        goto failed;
    ret->networks = virHashCreate(20);
    if (ret->networks == NULL)
        goto failed;
    ret->storagePools = virHashCreate(20);
    if (ret->storagePools == NULL)
        goto failed;
    ret->storageVols = virHashCreate(20);
    if (ret->storageVols == NULL)
        goto failed;

    pthread_mutex_init(&ret->lock, NULL);

    ret->refs = 1;
    return(ret);

failed:
    if (ret != NULL) {
        if (ret->domains != NULL)
            virHashFree(ret->domains, (virHashDeallocator) virDomainFreeName);
        if (ret->networks != NULL)
            virHashFree(ret->networks, (virHashDeallocator) virNetworkFreeName);
        if (ret->storagePools != NULL)
            virHashFree(ret->storagePools, (virHashDeallocator) virStoragePoolFreeName);
        if (ret->storageVols != NULL)
            virHashFree(ret->storageVols, (virHashDeallocator) virStorageVolFreeName);

        pthread_mutex_destroy(&ret->lock);
        VIR_FREE(ret);
    }
    return(NULL);
}

/**
 * virReleaseConnect:
 * @conn: the hypervisor connection to release
 *
 * Unconditionally release all memory associated with a connection.
 * The conn.lock mutex must be held prior to calling this, and will
 * be released prior to this returning. The connection obj must not
 * be used once this method returns.
 */
static void
virReleaseConnect(virConnectPtr conn) {
    DEBUG("release connection %p", conn);
    if (conn->domains != NULL)
        virHashFree(conn->domains, (virHashDeallocator) virDomainFreeName);
    if (conn->networks != NULL)
        virHashFree(conn->networks, (virHashDeallocator) virNetworkFreeName);
    if (conn->storagePools != NULL)
        virHashFree(conn->storagePools, (virHashDeallocator) virStoragePoolFreeName);
    if (conn->storageVols != NULL)
        virHashFree(conn->storageVols, (virHashDeallocator) virStorageVolFreeName);

    virResetError(&conn->err);
    if (virLastErr.conn == conn)
        virLastErr.conn = NULL;

    xmlFreeURI(conn->uri);

    pthread_mutex_unlock(&conn->lock);
    pthread_mutex_destroy(&conn->lock);
    VIR_FREE(conn);
}

/**
 * virUnrefConnect:
 * @conn: the hypervisor connection to unreference
 *
 * Unreference the connection. If the use count drops to zero, the structure is
 * actually freed.
 *
 * Returns the reference count or -1 in case of failure.
 */
int
virUnrefConnect(virConnectPtr conn) {
    int refs;

    if ((!VIR_IS_CONNECT(conn))) {
        virLibConnError(conn, VIR_ERR_INVALID_ARG, __FUNCTION__);
        return(-1);
    }
    pthread_mutex_lock(&conn->lock);
    DEBUG("unref connection %p %d", conn, conn->refs);
    conn->refs--;
    refs = conn->refs;
    if (refs == 0) {
        virReleaseConnect(conn);
        /* Already unlocked mutex */
        return (0);
    }
    pthread_mutex_unlock(&conn->lock);
    return (refs);
}

/**
 * virGetDomain:
 * @conn: the hypervisor connection
 * @name: pointer to the domain name
 * @uuid: pointer to the uuid
 *
 * Lookup if the domain is already registered for that connection,
 * if yes return a new pointer to it, if no allocate a new structure,
 * and register it in the table. In any case a corresponding call to
 * virUnrefDomain() is needed to not leak data.
 *
 * Returns a pointer to the domain, or NULL in case of failure
 */
virDomainPtr
virGetDomain(virConnectPtr conn, const char *name, const unsigned char *uuid) {
    virDomainPtr ret = NULL;

    if ((!VIR_IS_CONNECT(conn)) || (name == NULL) || (uuid == NULL)) {
        virLibConnError(conn, VIR_ERR_INVALID_ARG, __FUNCTION__);
        return(NULL);
    }
    pthread_mutex_lock(&conn->lock);

    /* TODO search by UUID first as they are better differenciators */

    ret = (virDomainPtr) virHashLookup(conn->domains, name);
    /* TODO check the UUID */
    if (ret == NULL) {
        if (VIR_ALLOC(ret) < 0) {
            virLibConnError(conn, VIR_ERR_NO_MEMORY, _("allocating domain"));
            goto error;
        }
        ret->name = strdup(name);
        if (ret->name == NULL) {
            virLibConnError(conn, VIR_ERR_NO_MEMORY, _("allocating domain"));
            goto error;
        }
        ret->magic = VIR_DOMAIN_MAGIC;
        ret->conn = conn;
        ret->id = -1;
        if (uuid != NULL)
            memcpy(&(ret->uuid[0]), uuid, VIR_UUID_BUFLEN);

        if (virHashAddEntry(conn->domains, name, ret) < 0) {
            virLibConnError(conn, VIR_ERR_INTERNAL_ERROR,
                            _("failed to add domain to connection hash table"));
            goto error;
        }
        conn->refs++;
        DEBUG("New hash entry %p", ret);
    } else {
        DEBUG("Existing hash entry %p: refs now %d", ret, ret->refs+1);
    }
    ret->refs++;
    pthread_mutex_unlock(&conn->lock);
    return(ret);

 error:
    pthread_mutex_unlock(&conn->lock);
    if (ret != NULL) {
        VIR_FREE(ret->name);
        VIR_FREE(ret);
    }
    return(NULL);
}

/**
 * virReleaseDomain:
 * @domain: the domain to release
 *
 * Unconditionally release all memory associated with a domain.
 * The conn.lock mutex must be held prior to calling this, and will
 * be released prior to this returning. The domain obj must not
 * be used once this method returns.
 *
 * It will also unreference the associated connection object,
 * which may also be released if its ref count hits zero.
 */
static void
virReleaseDomain(virDomainPtr domain) {
    virConnectPtr conn = domain->conn;
    DEBUG("release domain %p %s", domain, domain->name);

    /* TODO search by UUID first as they are better differenciators */
    if (virHashRemoveEntry(conn->domains, domain->name, NULL) < 0)
        virLibConnError(conn, VIR_ERR_INTERNAL_ERROR,
                        _("domain missing from connection hash table"));

    if (conn->err.dom == domain)
        conn->err.dom = NULL;
    if (virLastErr.dom == domain)
        virLastErr.dom = NULL;
    domain->magic = -1;
    domain->id = -1;
    VIR_FREE(domain->name);
    VIR_FREE(domain);

    DEBUG("unref connection %p %d", conn, conn->refs);
    conn->refs--;
    if (conn->refs == 0) {
        virReleaseConnect(conn);
        /* Already unlocked mutex */
        return;
    }

    pthread_mutex_unlock(&conn->lock);
}


/**
 * virUnrefDomain:
 * @domain: the domain to unreference
 *
 * Unreference the domain. If the use count drops to zero, the structure is
 * actually freed.
 *
 * Returns the reference count or -1 in case of failure.
 */
int
virUnrefDomain(virDomainPtr domain) {
    int refs;

    if (!VIR_IS_CONNECTED_DOMAIN(domain)) {
        virLibConnError(domain->conn, VIR_ERR_INVALID_ARG, __FUNCTION__);
        return(-1);
    }
    pthread_mutex_lock(&domain->conn->lock);
    DEBUG("unref domain %p %s %d", domain, domain->name, domain->refs);
    domain->refs--;
    refs = domain->refs;
    if (refs == 0) {
        virReleaseDomain(domain);
        /* Already unlocked mutex */
        return (0);
    }

    pthread_mutex_unlock(&domain->conn->lock);
    return (refs);
}

/**
 * virGetNetwork:
 * @conn: the hypervisor connection
 * @name: pointer to the network name
 * @uuid: pointer to the uuid
 *
 * Lookup if the network is already registered for that connection,
 * if yes return a new pointer to it, if no allocate a new structure,
 * and register it in the table. In any case a corresponding call to
 * virUnrefNetwork() is needed to not leak data.
 *
 * Returns a pointer to the network, or NULL in case of failure
 */
virNetworkPtr
virGetNetwork(virConnectPtr conn, const char *name, const unsigned char *uuid) {
    virNetworkPtr ret = NULL;

    if ((!VIR_IS_CONNECT(conn)) || (name == NULL) || (uuid == NULL)) {
        virLibConnError(conn, VIR_ERR_INVALID_ARG, __FUNCTION__);
        return(NULL);
    }
    pthread_mutex_lock(&conn->lock);

    /* TODO search by UUID first as they are better differenciators */

    ret = (virNetworkPtr) virHashLookup(conn->networks, name);
    /* TODO check the UUID */
    if (ret == NULL) {
        if (VIR_ALLOC(ret) < 0) {
            virLibConnError(conn, VIR_ERR_NO_MEMORY, _("allocating network"));
            goto error;
        }
        ret->name = strdup(name);
        if (ret->name == NULL) {
            virLibConnError(conn, VIR_ERR_NO_MEMORY, _("allocating network"));
            goto error;
        }
        ret->magic = VIR_NETWORK_MAGIC;
        ret->conn = conn;
        if (uuid != NULL)
            memcpy(&(ret->uuid[0]), uuid, VIR_UUID_BUFLEN);

        if (virHashAddEntry(conn->networks, name, ret) < 0) {
            virLibConnError(conn, VIR_ERR_INTERNAL_ERROR,
                            _("failed to add network to connection hash table"));
            goto error;
        }
        conn->refs++;
    }
    ret->refs++;
    pthread_mutex_unlock(&conn->lock);
    return(ret);

 error:
    pthread_mutex_unlock(&conn->lock);
    if (ret != NULL) {
        VIR_FREE(ret->name);
        VIR_FREE(ret);
    }
    return(NULL);
}

/**
 * virReleaseNetwork:
 * @network: the network to release
 *
 * Unconditionally release all memory associated with a network.
 * The conn.lock mutex must be held prior to calling this, and will
 * be released prior to this returning. The network obj must not
 * be used once this method returns.
 *
 * It will also unreference the associated connection object,
 * which may also be released if its ref count hits zero.
 */
static void
virReleaseNetwork(virNetworkPtr network) {
    virConnectPtr conn = network->conn;
    DEBUG("release network %p %s", network, network->name);

    /* TODO search by UUID first as they are better differenciators */
    if (virHashRemoveEntry(conn->networks, network->name, NULL) < 0)
        virLibConnError(conn, VIR_ERR_INTERNAL_ERROR,
                        _("network missing from connection hash table"));

    if (conn->err.net == network)
        conn->err.net = NULL;
    if (virLastErr.net == network)
        virLastErr.net = NULL;

    network->magic = -1;
    VIR_FREE(network->name);
    VIR_FREE(network);

    DEBUG("unref connection %p %d", conn, conn->refs);
    conn->refs--;
    if (conn->refs == 0) {
        virReleaseConnect(conn);
        /* Already unlocked mutex */
        return;
    }

    pthread_mutex_unlock(&conn->lock);
}


/**
 * virUnrefNetwork:
 * @network: the network to unreference
 *
 * Unreference the network. If the use count drops to zero, the structure is
 * actually freed.
 *
 * Returns the reference count or -1 in case of failure.
 */
int
virUnrefNetwork(virNetworkPtr network) {
    int refs;

    if (!VIR_IS_CONNECTED_NETWORK(network)) {
        virLibConnError(network->conn, VIR_ERR_INVALID_ARG, __FUNCTION__);
        return(-1);
    }
    pthread_mutex_lock(&network->conn->lock);
    DEBUG("unref network %p %s %d", network, network->name, network->refs);
    network->refs--;
    refs = network->refs;
    if (refs == 0) {
        virReleaseNetwork(network);
        /* Already unlocked mutex */
        return (0);
    }

    pthread_mutex_unlock(&network->conn->lock);
    return (refs);
}


/**
 * virGetStoragePool:
 * @conn: the hypervisor connection
 * @name: pointer to the storage pool name
 * @uuid: pointer to the uuid
 *
 * Lookup if the storage pool is already registered for that connection,
 * if yes return a new pointer to it, if no allocate a new structure,
 * and register it in the table. In any case a corresponding call to
 * virFreeStoragePool() is needed to not leak data.
 *
 * Returns a pointer to the network, or NULL in case of failure
 */
virStoragePoolPtr
virGetStoragePool(virConnectPtr conn, const char *name, const unsigned char *uuid) {
    virStoragePoolPtr ret = NULL;

    if ((!VIR_IS_CONNECT(conn)) || (name == NULL) || (uuid == NULL)) {
        virLibConnError(conn, VIR_ERR_INVALID_ARG, __FUNCTION__);
        return(NULL);
    }
    pthread_mutex_lock(&conn->lock);

    /* TODO search by UUID first as they are better differenciators */

    ret = (virStoragePoolPtr) virHashLookup(conn->storagePools, name);
    /* TODO check the UUID */
    if (ret == NULL) {
        if (VIR_ALLOC(ret) < 0) {
            virLibConnError(conn, VIR_ERR_NO_MEMORY, _("allocating storage pool"));
            goto error;
        }
        ret->name = strdup(name);
        if (ret->name == NULL) {
            virLibConnError(conn, VIR_ERR_NO_MEMORY, _("allocating storage pool"));
            goto error;
        }
        ret->magic = VIR_STORAGE_POOL_MAGIC;
        ret->conn = conn;
        if (uuid != NULL)
            memcpy(&(ret->uuid[0]), uuid, VIR_UUID_BUFLEN);

        if (virHashAddEntry(conn->storagePools, name, ret) < 0) {
            virLibConnError(conn, VIR_ERR_INTERNAL_ERROR,
                            _("failed to add storage pool to connection hash table"));
            goto error;
        }
        conn->refs++;
    }
    ret->refs++;
    pthread_mutex_unlock(&conn->lock);
    return(ret);

error:
    pthread_mutex_unlock(&conn->lock);
    if (ret != NULL) {
        VIR_FREE(ret->name);
        VIR_FREE(ret);
    }
    return(NULL);
}


/**
 * virReleaseStoragePool:
 * @pool: the pool to release
 *
 * Unconditionally release all memory associated with a pool.
 * The conn.lock mutex must be held prior to calling this, and will
 * be released prior to this returning. The pool obj must not
 * be used once this method returns.
 *
 * It will also unreference the associated connection object,
 * which may also be released if its ref count hits zero.
 */
static void
virReleaseStoragePool(virStoragePoolPtr pool) {
    virConnectPtr conn = pool->conn;
    DEBUG("release pool %p %s", pool, pool->name);

    /* TODO search by UUID first as they are better differenciators */
    if (virHashRemoveEntry(conn->storagePools, pool->name, NULL) < 0)
        virLibConnError(conn, VIR_ERR_INTERNAL_ERROR,
                        _("pool missing from connection hash table"));

    pool->magic = -1;
    VIR_FREE(pool->name);
    VIR_FREE(pool);

    DEBUG("unref connection %p %d", conn, conn->refs);
    conn->refs--;
    if (conn->refs == 0) {
        virReleaseConnect(conn);
        /* Already unlocked mutex */
        return;
    }

    pthread_mutex_unlock(&conn->lock);
}


/**
 * virUnrefStoragePool:
 * @pool: the pool to unreference
 *
 * Unreference the pool. If the use count drops to zero, the structure is
 * actually freed.
 *
 * Returns the reference count or -1 in case of failure.
 */
int
virUnrefStoragePool(virStoragePoolPtr pool) {
    int refs;

    if (!VIR_IS_CONNECTED_STORAGE_POOL(pool)) {
        virLibConnError(pool->conn, VIR_ERR_INVALID_ARG, __FUNCTION__);
        return(-1);
    }
    pthread_mutex_lock(&pool->conn->lock);
    DEBUG("unref pool %p %s %d", pool, pool->name, pool->refs);
    pool->refs--;
    refs = pool->refs;
    if (refs == 0) {
        virReleaseStoragePool(pool);
        /* Already unlocked mutex */
        return (0);
    }

    pthread_mutex_unlock(&pool->conn->lock);
    return (refs);
}


/**
 * virGetStorageVol:
 * @conn: the hypervisor connection
 * @pool: pool owning the volume
 * @name: pointer to the storage vol name
 * @uuid: pointer to the uuid
 *
 * Lookup if the storage vol is already registered for that connection,
 * if yes return a new pointer to it, if no allocate a new structure,
 * and register it in the table. In any case a corresponding call to
 * virFreeStorageVol() is needed to not leak data.
 *
 * Returns a pointer to the storage vol, or NULL in case of failure
 */
virStorageVolPtr
virGetStorageVol(virConnectPtr conn, const char *pool, const char *name, const char *key) {
    virStorageVolPtr ret = NULL;

    if ((!VIR_IS_CONNECT(conn)) || (name == NULL) || (key == NULL)) {
        virLibConnError(conn, VIR_ERR_INVALID_ARG, __FUNCTION__);
        return(NULL);
    }
    pthread_mutex_lock(&conn->lock);

    ret = (virStorageVolPtr) virHashLookup(conn->storageVols, key);
    if (ret == NULL) {
        if (VIR_ALLOC(ret) < 0) {
            virLibConnError(conn, VIR_ERR_NO_MEMORY, _("allocating storage vol"));
            goto error;
        }
        ret->pool = strdup(pool);
        if (ret->pool == NULL) {
            virLibConnError(conn, VIR_ERR_NO_MEMORY, _("allocating storage vol"));
            goto error;
        }
        ret->name = strdup(name);
        if (ret->name == NULL) {
            virLibConnError(conn, VIR_ERR_NO_MEMORY, _("allocating storage vol"));
            goto error;
        }
        strncpy(ret->key, key, sizeof(ret->key)-1);
        ret->key[sizeof(ret->key)-1] = '\0';
        ret->magic = VIR_STORAGE_VOL_MAGIC;
        ret->conn = conn;

        if (virHashAddEntry(conn->storageVols, key, ret) < 0) {
            virLibConnError(conn, VIR_ERR_INTERNAL_ERROR,
                            _("failed to add storage vol to connection hash table"));
            goto error;
        }
        conn->refs++;
    }
    ret->refs++;
    pthread_mutex_unlock(&conn->lock);
    return(ret);

error:
    pthread_mutex_unlock(&conn->lock);
    if (ret != NULL) {
        VIR_FREE(ret->name);
        VIR_FREE(ret->pool);
        VIR_FREE(ret);
    }
    return(NULL);
}


/**
 * virReleaseStorageVol:
 * @vol: the vol to release
 *
 * Unconditionally release all memory associated with a vol.
 * The conn.lock mutex must be held prior to calling this, and will
 * be released prior to this returning. The vol obj must not
 * be used once this method returns.
 *
 * It will also unreference the associated connection object,
 * which may also be released if its ref count hits zero.
 */
static void
virReleaseStorageVol(virStorageVolPtr vol) {
    virConnectPtr conn = vol->conn;
    DEBUG("release vol %p %s", vol, vol->name);

    /* TODO search by UUID first as they are better differenciators */
    if (virHashRemoveEntry(conn->storageVols, vol->key, NULL) < 0)
        virLibConnError(conn, VIR_ERR_INTERNAL_ERROR,
                        _("vol missing from connection hash table"));

    vol->magic = -1;
    VIR_FREE(vol->name);
    VIR_FREE(vol->pool);
    VIR_FREE(vol);

    DEBUG("unref connection %p %d", conn, conn->refs);
    conn->refs--;
    if (conn->refs == 0) {
        virReleaseConnect(conn);
        /* Already unlocked mutex */
        return;
    }

    pthread_mutex_unlock(&conn->lock);
}


/**
 * virUnrefStorageVol:
 * @vol: the vol to unreference
 *
 * Unreference the vol. If the use count drops to zero, the structure is
 * actually freed.
 *
 * Returns the reference count or -1 in case of failure.
 */
int
virUnrefStorageVol(virStorageVolPtr vol) {
    int refs;

    if (!VIR_IS_CONNECTED_STORAGE_VOL(vol)) {
        virLibConnError(vol->conn, VIR_ERR_INVALID_ARG, __FUNCTION__);
        return(-1);
    }
    pthread_mutex_lock(&vol->conn->lock);
    DEBUG("unref vol %p %s %d", vol, vol->name, vol->refs);
    vol->refs--;
    refs = vol->refs;
    if (refs == 0) {
        virReleaseStorageVol(vol);
        /* Already unlocked mutex */
        return (0);
    }

    pthread_mutex_unlock(&vol->conn->lock);
    return (refs);
}
