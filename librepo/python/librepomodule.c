/* librepo - A library providing (libcURL like) API to downloading repository
 * Copyright (C) 2012  Tomas Mlcoch
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <Python.h>
#include <glib.h>

#include "librepo/librepo.h"

#include "exception-py.h"
#include "handle-py.h"
#include "packagedownloader-py.h"
#include "packagetarget-py.h"
#include "result-py.h"
#include "yum-py.h"
#include "downloader-py.h"
#include "globalstate-py.h" // GIL Hack
#include "typeconversion.h"

volatile int global_logger = 0;
volatile PyThreadState **global_state = NULL;

PyObject *debug_cb = NULL;
PyObject *debug_cb_data = NULL;
gint      debug_handler_id = -1;

G_LOCK_DEFINE(gil_hack_lock);

void
py_debug_cb(G_GNUC_UNUSED const gchar *log_domain,
            G_GNUC_UNUSED GLogLevelFlags log_level,
            const gchar *message,
            G_GNUC_UNUSED gpointer user_data)
{
    PyObject *arglist, *data, *result, *py_message;

    if (!debug_cb)
        return;

    // XXX: GIL Hack
    if (global_state)
        EndAllowThreads((PyThreadState **) global_state);
    // XXX: End of GIL Hack

    py_message = PyStringOrNone_FromString(message);
    data = (debug_cb_data) ? debug_cb_data : Py_None;
    arglist = Py_BuildValue("(OO)", py_message, data);
    result = PyObject_CallObject(debug_cb, arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);
    Py_DECREF(py_message);

    // XXX: GIL Hack
    if (global_state)
        BeginAllowThreads((PyThreadState **) global_state);
    // XXX: End of GIL Hack
}

PyObject *
py_set_debug_log_handler(G_GNUC_UNUSED PyObject *self, PyObject *args)
{
    PyObject *cb, *cb_data = NULL;

    if (!PyArg_ParseTuple(args, "O|O:py_set_debug_log_handler", &cb, &cb_data))
        return NULL;

    if (cb == Py_None)
        cb = NULL;

    if (cb && !PyCallable_Check(cb)) {
        PyErr_SetString(PyExc_TypeError, "parameter must be callable");
       return NULL;
    }

    Py_XDECREF(debug_cb);
    Py_XDECREF(debug_cb_data);

    debug_cb      = cb;
    debug_cb_data = cb_data;

    Py_XINCREF(debug_cb);
    Py_XINCREF(debug_cb_data);

    if (debug_cb) {
        debug_handler_id = g_log_set_handler("librepo", G_LOG_LEVEL_DEBUG,
                                             py_debug_cb, NULL);
        global_logger = 1;
    } else if (debug_handler_id != -1) {
        g_log_remove_handler("librepo", debug_handler_id);
    }

    Py_RETURN_NONE;
}

static struct PyMethodDef librepo_methods[] = {
    { "yum_repomd_get_age",     (PyCFunction)py_yum_repomd_get_age,
      METH_VARARGS, NULL },
    { "set_debug_log_handler",  (PyCFunction)py_set_debug_log_handler,
      METH_VARARGS, NULL },
    { "download_packages",      (PyCFunction)py_download_packages,
      METH_VARARGS, NULL },
    { "download_url",           (PyCFunction)py_download_url,
      METH_VARARGS, NULL },
    { NULL }
};

void
exit_librepo(void)
{
    Py_XDECREF(debug_cb);
    Py_XDECREF(debug_cb_data);
    Py_XDECREF(LrErr_Exception);
}

struct module_state {
    PyObject *error;
};

#if PY_MAJOR_VERSION >= 3
#define GETSTATE(m) ((struct module_state*)PyModule_GetState(m))
#else
#define GETSTATE(m) (&_state)
static struct module_state _state;
#endif

#if PY_MAJOR_VERSION >= 3

static int librepo_traverse(PyObject *m, visitproc visit, void *arg) {
    Py_VISIT(GETSTATE(m)->error);
        return 0;
}

static int librepo_clear(PyObject *m) {
    Py_CLEAR(GETSTATE(m)->error);
        return 0;
}

static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    "_librepo",
    "A library providing C and Python (libcURL like) API for downloading "
    "linux repository metadata and packages",
    sizeof(struct module_state),
    librepo_methods,
    NULL,
    librepo_traverse,
    librepo_clear,
    NULL
};

#define INITERROR return NULL

PyObject *
PyInit__librepo(void)

#else
#define INITERROR return

void
init_librepo(void)
#endif
{
#if PY_MAJOR_VERSION >= 3
    PyObject *m = PyModule_Create(&moduledef);
#else
    PyObject *m = Py_InitModule("_librepo", librepo_methods);
#endif

    if (!m)
        INITERROR;

    struct module_state *st = GETSTATE(m);

    // Exceptions
    if (!init_exceptions()) {
        Py_DECREF(m);
        INITERROR;
    }

    st->error = LrErr_Exception;

    PyModule_AddObject(m, "LibrepoException", LrErr_Exception);

    // Objects

    // _librepo.Handle
    if (PyType_Ready(&Handle_Type) < 0)
        INITERROR;
    Py_INCREF(&Handle_Type);
    PyModule_AddObject(m, "Handle", (PyObject *)&Handle_Type);

    // _librepo.Result
    if (PyType_Ready(&Result_Type) < 0)
        INITERROR;
    Py_INCREF(&Result_Type);
    PyModule_AddObject(m, "Result", (PyObject *)&Result_Type);

    // _librepo.PackageTarget
    if (PyType_Ready(&PackageTarget_Type) < 0)
        INITERROR;
    Py_INCREF(&PackageTarget_Type);
    PyModule_AddObject(m, "PackageTarget", (PyObject *)&PackageTarget_Type);

    // Init module
    Py_AtExit(exit_librepo);

    // Module constants

#define PYMODULE_ADDINTCONSTANT(name) PyModule_AddIntConstant(m, #name, (name))
#define PYMODULE_ADDSTRCONSTANT(name) PyModule_AddStringConstant(m, #name, (name))

    // Version
    PYMODULE_ADDINTCONSTANT(LR_VERSION_MAJOR);
    PYMODULE_ADDINTCONSTANT(LR_VERSION_MINOR);
    PYMODULE_ADDINTCONSTANT(LR_VERSION_PATCH);
    PYMODULE_ADDSTRCONSTANT(LR_VERSION);

    // Handle options
    PYMODULE_ADDINTCONSTANT(LRO_UPDATE);
    PYMODULE_ADDINTCONSTANT(LRO_URLS);
    PYMODULE_ADDINTCONSTANT(LRO_MIRRORLIST);
    PYMODULE_ADDINTCONSTANT(LRO_MIRRORLISTURL);
    PYMODULE_ADDINTCONSTANT(LRO_METALINKURL);
    PYMODULE_ADDINTCONSTANT(LRO_LOCAL);
    PYMODULE_ADDINTCONSTANT(LRO_HTTPAUTH);
    PYMODULE_ADDINTCONSTANT(LRO_USERPWD);
    PYMODULE_ADDINTCONSTANT(LRO_PROXY);
    PYMODULE_ADDINTCONSTANT(LRO_PROXYPORT);
    PYMODULE_ADDINTCONSTANT(LRO_PROXYTYPE);
    PYMODULE_ADDINTCONSTANT(LRO_PROXYAUTH);
    PYMODULE_ADDINTCONSTANT(LRO_PROXYUSERPWD);
    PYMODULE_ADDINTCONSTANT(LRO_PROGRESSCB);
    PYMODULE_ADDINTCONSTANT(LRO_PROGRESSDATA);
    PYMODULE_ADDINTCONSTANT(LRO_MAXSPEED);
    PYMODULE_ADDINTCONSTANT(LRO_DESTDIR);
    PYMODULE_ADDINTCONSTANT(LRO_REPOTYPE);
    PYMODULE_ADDINTCONSTANT(LRO_CONNECTTIMEOUT);
    PYMODULE_ADDINTCONSTANT(LRO_IGNOREMISSING);
    PYMODULE_ADDINTCONSTANT(LRO_INTERRUPTIBLE);
    PYMODULE_ADDINTCONSTANT(LRO_USERAGENT);
    PYMODULE_ADDINTCONSTANT(LRO_FETCHMIRRORS);
    PYMODULE_ADDINTCONSTANT(LRO_MAXMIRRORTRIES);
    PYMODULE_ADDINTCONSTANT(LRO_MAXPARALLELDOWNLOADS);
    PYMODULE_ADDINTCONSTANT(LRO_MAXDOWNLOADSPERMIRROR);
    PYMODULE_ADDINTCONSTANT(LRO_VARSUB);
    PYMODULE_ADDINTCONSTANT(LRO_FASTESTMIRROR);
    PYMODULE_ADDINTCONSTANT(LRO_FASTESTMIRRORCACHE);
    PYMODULE_ADDINTCONSTANT(LRO_FASTESTMIRRORMAXAGE);
    PYMODULE_ADDINTCONSTANT(LRO_FASTESTMIRRORCB);
    PYMODULE_ADDINTCONSTANT(LRO_FASTESTMIRRORDATA);
    PYMODULE_ADDINTCONSTANT(LRO_LOWSPEEDTIME);
    PYMODULE_ADDINTCONSTANT(LRO_LOWSPEEDLIMIT);
    PYMODULE_ADDINTCONSTANT(LRO_GPGCHECK);
    PYMODULE_ADDINTCONSTANT(LRO_CHECKSUM);
    PYMODULE_ADDINTCONSTANT(LRO_YUMDLIST);
    PYMODULE_ADDINTCONSTANT(LRO_YUMBLIST);
    PYMODULE_ADDINTCONSTANT(LRO_HMFCB);
    PYMODULE_ADDINTCONSTANT(LRO_SSLVERIFYPEER);
    PYMODULE_ADDINTCONSTANT(LRO_SSLVERIFYHOST);
    PYMODULE_ADDINTCONSTANT(LRO_IPRESOLVE);
    PYMODULE_ADDINTCONSTANT(LRO_ALLOWEDMIRRORFAILURES);
    PYMODULE_ADDINTCONSTANT(LRO_ADAPTIVEMIRRORSORTING);
    PYMODULE_ADDINTCONSTANT(LRO_GNUPGHOMEDIR);
    PYMODULE_ADDINTCONSTANT(LRO_FASTESTMIRRORTIMEOUT);
    PYMODULE_ADDINTCONSTANT(LRO_HTTPHEADER);
    PYMODULE_ADDINTCONSTANT(LRO_SENTINEL);

    // Handle info options
    PYMODULE_ADDINTCONSTANT(LRI_UPDATE);
    PYMODULE_ADDINTCONSTANT(LRI_URLS);
    PYMODULE_ADDINTCONSTANT(LRI_MIRRORLIST);
    PYMODULE_ADDINTCONSTANT(LRI_MIRRORLISTURL);
    PYMODULE_ADDINTCONSTANT(LRI_METALINKURL);
    PYMODULE_ADDINTCONSTANT(LRI_LOCAL);
    PYMODULE_ADDINTCONSTANT(LRI_PROGRESSCB);
    PYMODULE_ADDINTCONSTANT(LRI_PROGRESSDATA);
    PYMODULE_ADDINTCONSTANT(LRI_DESTDIR);
    PYMODULE_ADDINTCONSTANT(LRI_REPOTYPE);
    PYMODULE_ADDINTCONSTANT(LRI_USERAGENT);
    PYMODULE_ADDINTCONSTANT(LRI_YUMDLIST);
    PYMODULE_ADDINTCONSTANT(LRI_YUMBLIST);
    PYMODULE_ADDINTCONSTANT(LRI_FETCHMIRRORS);
    PYMODULE_ADDINTCONSTANT(LRI_MAXMIRRORTRIES);
    PYMODULE_ADDINTCONSTANT(LRI_VARSUB);
    PYMODULE_ADDINTCONSTANT(LRI_MIRRORS);
    PYMODULE_ADDINTCONSTANT(LRI_METALINK);
    PYMODULE_ADDINTCONSTANT(LRI_FASTESTMIRROR);
    PYMODULE_ADDINTCONSTANT(LRI_FASTESTMIRRORCACHE);
    PYMODULE_ADDINTCONSTANT(LRI_FASTESTMIRRORMAXAGE);
    PYMODULE_ADDINTCONSTANT(LRI_HMFCB);
    PYMODULE_ADDINTCONSTANT(LRI_SSLVERIFYPEER);
    PYMODULE_ADDINTCONSTANT(LRI_SSLVERIFYHOST);
    PYMODULE_ADDINTCONSTANT(LRI_IPRESOLVE);
    PYMODULE_ADDINTCONSTANT(LRI_ALLOWEDMIRRORFAILURES);
    PYMODULE_ADDINTCONSTANT(LRI_ADAPTIVEMIRRORSORTING);
    PYMODULE_ADDINTCONSTANT(LRI_GNUPGHOMEDIR);
    PYMODULE_ADDINTCONSTANT(LRI_FASTESTMIRRORTIMEOUT);
    PYMODULE_ADDINTCONSTANT(LRI_HTTPHEADER);
    PYMODULE_ADDINTCONSTANT(LRI_SENTINEL);

    // Check options
    PYMODULE_ADDINTCONSTANT(LR_CHECK_GPG);
    PYMODULE_ADDINTCONSTANT(LR_CHECK_CHECKSUM);

    // Repo type
    PYMODULE_ADDINTCONSTANT(LR_YUMREPO);
    PYMODULE_ADDINTCONSTANT(LR_SUSEREPO);
    PYMODULE_ADDINTCONSTANT(LR_DEBREPO);

    // Proxy type
    PYMODULE_ADDINTCONSTANT(LR_PROXY_HTTP);
    PYMODULE_ADDINTCONSTANT(LR_PROXY_HTTP_1_0);
    PYMODULE_ADDINTCONSTANT(LR_PROXY_SOCKS4);
    PYMODULE_ADDINTCONSTANT(LR_PROXY_SOCKS5);
    PYMODULE_ADDINTCONSTANT(LR_PROXY_SOCKS4A);
    PYMODULE_ADDINTCONSTANT(LR_PROXY_SOCKS5_HOSTNAME);

    // IpResolve type
    PYMODULE_ADDINTCONSTANT(LR_IPRESOLVE_WHATEVER);
    PYMODULE_ADDINTCONSTANT(LR_IPRESOLVE_V4);
    PYMODULE_ADDINTCONSTANT(LR_IPRESOLVE_V6);

    // Return codes
    PYMODULE_ADDINTCONSTANT(LRE_OK);
    PYMODULE_ADDINTCONSTANT(LRE_BADFUNCARG);
    PYMODULE_ADDINTCONSTANT(LRE_BADOPTARG);
    PYMODULE_ADDINTCONSTANT(LRE_UNKNOWNOPT);
    PYMODULE_ADDINTCONSTANT(LRE_CURLSETOPT);
    PYMODULE_ADDINTCONSTANT(LRE_ALREADYUSEDRESULT);
    PYMODULE_ADDINTCONSTANT(LRE_INCOMPLETERESULT);
    PYMODULE_ADDINTCONSTANT(LRE_CURLDUP);
    PYMODULE_ADDINTCONSTANT(LRE_CURL);
    PYMODULE_ADDINTCONSTANT(LRE_CURLM);
    PYMODULE_ADDINTCONSTANT(LRE_BADSTATUS);
    PYMODULE_ADDINTCONSTANT(LRE_TEMPORARYERR);
    PYMODULE_ADDINTCONSTANT(LRE_NOTLOCAL);
    PYMODULE_ADDINTCONSTANT(LRE_CANNOTCREATEDIR);
    PYMODULE_ADDINTCONSTANT(LRE_IO);
    PYMODULE_ADDINTCONSTANT(LRE_MLBAD);
    PYMODULE_ADDINTCONSTANT(LRE_MLXML);
    PYMODULE_ADDINTCONSTANT(LRE_BADCHECKSUM);
    PYMODULE_ADDINTCONSTANT(LRE_REPOMDXML);
    PYMODULE_ADDINTCONSTANT(LRE_NOURL);
    PYMODULE_ADDINTCONSTANT(LRE_CANNOTCREATETMP);
    PYMODULE_ADDINTCONSTANT(LRE_UNKNOWNCHECKSUM);
    PYMODULE_ADDINTCONSTANT(LRE_BADURL);
    PYMODULE_ADDINTCONSTANT(LRE_GPGNOTSUPPORTED);
    PYMODULE_ADDINTCONSTANT(LRE_GPGERROR);
    PYMODULE_ADDINTCONSTANT(LRE_BADGPG);
    PYMODULE_ADDINTCONSTANT(LRE_INCOMPLETEREPO);
    PYMODULE_ADDINTCONSTANT(LRE_INTERRUPTED);
    PYMODULE_ADDINTCONSTANT(LRE_SIGACTION);
    PYMODULE_ADDINTCONSTANT(LRE_ALREADYDOWNLOADED);
    PYMODULE_ADDINTCONSTANT(LRE_UNFINISHED);
    PYMODULE_ADDINTCONSTANT(LRE_SELECT);
    PYMODULE_ADDINTCONSTANT(LRE_OPENSSL);
    PYMODULE_ADDINTCONSTANT(LRE_MEMORY);
    PYMODULE_ADDINTCONSTANT(LRE_XMLPARSER);
    PYMODULE_ADDINTCONSTANT(LRE_CBINTERRUPTED);
    PYMODULE_ADDINTCONSTANT(LRE_REPOMD);
    PYMODULE_ADDINTCONSTANT(LRE_VALUE);
    PYMODULE_ADDINTCONSTANT(LRE_NOTSET);
    PYMODULE_ADDINTCONSTANT(LRE_FILE);
    PYMODULE_ADDINTCONSTANT(LRE_KEYFILE);
    PYMODULE_ADDINTCONSTANT(LRE_UNKNOWNERROR);


    // Result option
    PYMODULE_ADDINTCONSTANT(LRR_YUM_REPO);
    PYMODULE_ADDINTCONSTANT(LRR_YUM_REPOMD);
    PYMODULE_ADDINTCONSTANT(LRR_YUM_TIMESTAMP);
    PYMODULE_ADDINTCONSTANT(LRR_SENTINEL);

    // Checksums
    PYMODULE_ADDINTCONSTANT(LR_CHECKSUM_UNKNOWN);
    PYMODULE_ADDINTCONSTANT(LR_CHECKSUM_MD5);
    PYMODULE_ADDINTCONSTANT(LR_CHECKSUM_SHA1);
    PYMODULE_ADDINTCONSTANT(LR_CHECKSUM_SHA224);
    PYMODULE_ADDINTCONSTANT(LR_CHECKSUM_SHA256);
    PYMODULE_ADDINTCONSTANT(LR_CHECKSUM_SHA384);
    PYMODULE_ADDINTCONSTANT(LR_CHECKSUM_SHA512);

    // Transfer statuses
    PYMODULE_ADDINTCONSTANT(LR_TRANSFER_SUCCESSFUL);
    PYMODULE_ADDINTCONSTANT(LR_TRANSFER_ALREDYEXISTS);
    PYMODULE_ADDINTCONSTANT(LR_TRANSFER_ERROR);

    // Fastest mirror stages
    PYMODULE_ADDINTCONSTANT(LR_FMSTAGE_INIT);
    PYMODULE_ADDINTCONSTANT(LR_FMSTAGE_CACHELOADING);
    PYMODULE_ADDINTCONSTANT(LR_FMSTAGE_CACHELOADINGSTATUS);
    PYMODULE_ADDINTCONSTANT(LR_FMSTAGE_DETECTION);
    PYMODULE_ADDINTCONSTANT(LR_FMSTAGE_FINISHING);
    PYMODULE_ADDINTCONSTANT(LR_FMSTAGE_STATUS);

    // Callbacks return values
    PYMODULE_ADDINTCONSTANT(LR_CB_OK);
    PYMODULE_ADDINTCONSTANT(LR_CB_ABORT);
    PYMODULE_ADDINTCONSTANT(LR_CB_ERROR);

#if PY_MAJOR_VERSION >= 3
    return m;
#endif
}
