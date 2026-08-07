

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


 /* File created by MIDL compiler version 8.01.0628 */
/* at Tue Jan 19 11:14:07 2038
 */
/* Compiler settings for idl\service.idl:
    Oicf, W1, Zp8, env=Win32 (32b run), target_arch=X86 8.01.0628 
    protocol : dce , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
/* @@MIDL_FILE_HEADING(  ) */



/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 500
#endif

#include "rpc.h"
#include "rpcndr.h"

#ifndef __RPCNDR_H_VERSION__
#error this stub requires an updated version of <rpcndr.h>
#endif /* __RPCNDR_H_VERSION__ */


#ifndef __service_h_h__
#define __service_h_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

#ifndef DECLSPEC_XFGVIRT
#if defined(_CONTROL_FLOW_GUARD_XFG)
#define DECLSPEC_XFGVIRT(base, func) __declspec(xfg_virtual(base, func))
#else
#define DECLSPEC_XFGVIRT(base, func)
#endif
#endif

/* Forward Declarations */ 

#ifdef __cplusplus
extern "C"{
#endif 


#ifndef __service_INTERFACE_DEFINED__
#define __service_INTERFACE_DEFINED__

/* interface service */
/* [version][uuid] */ 

unsigned long long login( 
    /* [in] */ handle_t IDL_handle,
    /* [in] */ unsigned int pid,
    /* [in] */ unsigned long long envFlag);

void request_window_inspection( 
    /* [in] */ handle_t IDL_handle,
    /* [in] */ unsigned int pid,
    /* [in] */ unsigned long long envFlag);

void require_elevation( 
    /* [in] */ handle_t IDL_handle,
    /* [in] */ unsigned int pid,
    /* [in] */ unsigned long long envFlag,
    /* [string][in] */ const wchar_t path[  ]);

int contains_process_id_exclude( 
    /* [in] */ handle_t IDL_handle,
    /* [in] */ unsigned long pid,
    /* [in] */ unsigned long long excludeEnvFlag);

#define	MAX_PIDS	( 2048 )

void get_all_process_id_exclude( 
    /* [in] */ handle_t IDL_handle,
    /* [in] */ unsigned long long excludeEnvFlag,
    /* [size_is][length_is][out] */ unsigned long long pids[  ],
    /* [out] */ unsigned int *count);

void add_toplevel_window( 
    /* [in] */ handle_t IDL_handle,
    /* [in] */ unsigned long long hWnd,
    /* [in] */ unsigned int pid,
    /* [in] */ unsigned long long envFlag);

void remove_toplevel_window( 
    /* [in] */ handle_t IDL_handle,
    /* [in] */ unsigned long long hWnd,
    /* [in] */ unsigned int pid,
    /* [in] */ unsigned long long envFlag);

int contains_toplevel_window_exclude( 
    /* [in] */ handle_t IDL_handle,
    /* [in] */ unsigned long long hWnd,
    /* [in] */ unsigned long long excludeEnvFlag);

#define	MAX_TOPLEVEL_WINDOW	( 2048 )

void get_all_toplevel_window_exclude( 
    /* [in] */ handle_t IDL_handle,
    /* [in] */ unsigned long long excludeEnvFlag,
    /* [size_is][length_is][out] */ unsigned long long hWnds[  ],
    /* [out] */ unsigned int *count);

void create_redirect_file( 
    /* [in] */ handle_t IDL_handle,
    /* [string][in] */ const wchar_t originalFile[  ],
    /* [string][in] */ const wchar_t redirectFile[  ]);

void create_process( 
    /* [in] */ handle_t IDL_handle,
    /* [string][in] */ const wchar_t appPath[  ],
    /* [string][in] */ const wchar_t params[  ]);



extern RPC_IF_HANDLE service_v1_0_c_ifspec;
extern RPC_IF_HANDLE service_v1_0_s_ifspec;
#endif /* __service_INTERFACE_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif


