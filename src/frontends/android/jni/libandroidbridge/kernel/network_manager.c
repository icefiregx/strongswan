/*
 * Copyright (C) 2012 Tobias Brunner
 * Hochschule fuer Technik Rapperswil
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.  *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include "network_manager.h"

#include "../android_jni.h"
#include "../charonservice.h"
#include <utils/debug.h>
#include <threading/mutex.h>

typedef struct private_network_manager_t private_network_manager_t;

struct private_network_manager_t {

	/**
	 * Public interface
	 */
	network_manager_t public;

	/**
	 * Reference to NetworkManager object
	 */
	jobject obj;

	/**
	 * Java class for NetworkManager
	 */
	jclass cls;

	/**
	 * Registered callback
	 */
	struct {
		connectivity_cb_t cb;
		void *data;
	} connectivity_cb;

	/**
	 * Mutex to access callback
	 */
	mutex_t *mutex;
};

METHOD(network_manager_t, get_local_address, host_t*,
	private_network_manager_t *this, bool ipv4)
{
	JNIEnv *env;
	jmethodID method_id;
	jstring jaddr;
	char *addr;
	host_t *host;

	androidjni_attach_thread(&env);
	method_id = (*env)->GetMethodID(env, this->cls, "getLocalAddress",
									"(Z)Ljava/lang/String;");
	if (!method_id)
	{
		goto failed;
	}
	jaddr = (*env)->CallObjectMethod(env, this->obj, method_id, ipv4);
	if (!jaddr)
	{
		goto failed;
	}
	addr = androidjni_convert_jstring(env, jaddr);
	androidjni_detach_thread();
	host = host_create_from_string(addr, 0);
	free(addr);
	return host;

failed:
	androidjni_exception_occurred(env);
	androidjni_detach_thread();
	return NULL;
}

JNI_METHOD(NetworkManager, networkChanged, void,
	bool disconnected)
{
	private_network_manager_t *nm;

	nm = (private_network_manager_t*)charonservice->get_network_manager(
																charonservice);
	nm->mutex->lock(nm->mutex);
	if (nm->connectivity_cb.cb)
	{
		nm->connectivity_cb.cb(nm->connectivity_cb.data, disconnected);
	}
	nm->mutex->unlock(nm->mutex);
}

METHOD(network_manager_t, add_connectivity_cb, void,
	private_network_manager_t *this, connectivity_cb_t cb, void *data)
{
	this->mutex->lock(this->mutex);
	if (!this->connectivity_cb.cb)
	{
		JNIEnv *env;
		jmethodID method_id;

		androidjni_attach_thread(&env);
		method_id = (*env)->GetMethodID(env, this->cls, "Register", "()V");
		if (!method_id)
		{
			androidjni_exception_occurred(env);
		}
		else
		{
			(*env)->CallVoidMethod(env, this->obj, method_id);
			if (!androidjni_exception_occurred(env))
			{
				this->connectivity_cb.cb = cb;
				this->connectivity_cb.data = data;
			}
			androidjni_detach_thread();
		}
	}
	this->mutex->unlock(this->mutex);
}

/**
 * Unregister the NetworkManager via JNI.
 *
 * this->mutex has to be locked
 */
static void unregister_network_manager(private_network_manager_t *this)
{
	JNIEnv *env;
	jmethodID method_id;

	androidjni_attach_thread(&env);
	method_id = (*env)->GetMethodID(env, this->cls, "Unregister", "()V");
	if (!method_id)
	{
		androidjni_exception_occurred(env);
	}
	else
	{
		(*env)->CallVoidMethod(env, this->obj, method_id);
		androidjni_exception_occurred(env);
	}
	androidjni_detach_thread();
}

METHOD(network_manager_t, remove_connectivity_cb, void,
	private_network_manager_t *this, connectivity_cb_t cb)
{
	this->mutex->lock(this->mutex);
	if (this->connectivity_cb.cb == cb)
	{
		this->connectivity_cb.cb = NULL;
		unregister_network_manager(this);
	}
	this->mutex->unlock(this->mutex);
}

METHOD(network_manager_t, destroy, void,
	private_network_manager_t *this)
{
	JNIEnv *env;

	this->mutex->lock(this->mutex);
	if (this->connectivity_cb.cb)
	{
		this->connectivity_cb.cb = NULL;
		unregister_network_manager(this);
	}
	this->mutex->unlock(this->mutex);

	androidjni_attach_thread(&env);
	if (this->obj)
	{
		(*env)->DeleteGlobalRef(env, this->obj);
	}
	if (this->cls)
	{
		(*env)->DeleteGlobalRef(env, this->cls);
	}
	androidjni_detach_thread();
	this->mutex->destroy(this->mutex);
	free(this);
}

/*
 * Described in header.
 */
network_manager_t *network_manager_create(jobject context)
{
	private_network_manager_t *this;
	JNIEnv *env;
	jmethodID method_id;
	jobject obj;
	jclass cls;

	INIT(this,
		.public = {
			.get_local_address = _get_local_address,
			.add_connectivity_cb = _add_connectivity_cb,
			.remove_connectivity_cb = _remove_connectivity_cb,
			.destroy = _destroy,
		},
		.mutex = mutex_create(MUTEX_TYPE_DEFAULT),
	);

	androidjni_attach_thread(&env);
	cls = (*env)->FindClass(env, JNI_PACKAGE_STRING "/NetworkManager");
	if (!cls)
	{
		goto failed;
	}
	this->cls = (*env)->NewGlobalRef(env, cls);
	method_id = (*env)->GetMethodID(env, cls, "<init>",
									"(Landroid/content/Context;)V");
	if (!method_id)
	{
		goto failed;
	}
	obj = (*env)->NewObject(env, cls, method_id, context);
	if (!obj)
	{
		goto failed;
	}
	this->obj = (*env)->NewGlobalRef(env, obj);
	androidjni_detach_thread();
	return &this->public;

failed:
	DBG1(DBG_KNL, "failed to build NetworkManager object");
	androidjni_exception_occurred(env);
	androidjni_detach_thread();
	destroy(this);
	return NULL;
};
