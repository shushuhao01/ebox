

/* this ALWAYS GENERATED file contains the RPC client stubs */


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

#if !defined(_M_IA64) && !defined(_M_AMD64) && !defined(_ARM_)


#if _MSC_VER >= 1200
#pragma warning(push)
#endif

#pragma warning( disable: 4211 )  /* redefine extern to static */
#pragma warning( disable: 4232 )  /* dllimport identity*/
#pragma warning( disable: 4024 )  /* array to pointer mapping*/
#pragma warning( disable: 4100 ) /* unreferenced arguments in x86 call */

#pragma optimize("", off ) 

#include <string.h>

#include "service_h.h"

#define TYPE_FORMAT_STRING_SIZE   27                                
#define PROC_FORMAT_STRING_SIZE   489                               
#define EXPR_FORMAT_STRING_SIZE   1                                 
#define TRANSMIT_AS_TABLE_SIZE    0            
#define WIRE_MARSHAL_TABLE_SIZE   0            

typedef struct _service_MIDL_TYPE_FORMAT_STRING
    {
    short          Pad;
    unsigned char  Format[ TYPE_FORMAT_STRING_SIZE ];
    } service_MIDL_TYPE_FORMAT_STRING;

typedef struct _service_MIDL_PROC_FORMAT_STRING
    {
    short          Pad;
    unsigned char  Format[ PROC_FORMAT_STRING_SIZE ];
    } service_MIDL_PROC_FORMAT_STRING;

typedef struct _service_MIDL_EXPR_FORMAT_STRING
    {
    long          Pad;
    unsigned char  Format[ EXPR_FORMAT_STRING_SIZE ];
    } service_MIDL_EXPR_FORMAT_STRING;


static const RPC_SYNTAX_IDENTIFIER  _RpcTransferSyntax_2_0 = 
{{0x8A885D04,0x1CEB,0x11C9,{0x9F,0xE8,0x08,0x00,0x2B,0x10,0x48,0x60}},{2,0}};

#if defined(_CONTROL_FLOW_GUARD_XFG)
#define XFG_TRAMPOLINES(ObjectType)\
NDR_SHAREABLE unsigned long ObjectType ## _UserSize_XFG(unsigned long * pFlags, unsigned long Offset, void * pObject)\
{\
return  ObjectType ## _UserSize(pFlags, Offset, (ObjectType *)pObject);\
}\
NDR_SHAREABLE unsigned char * ObjectType ## _UserMarshal_XFG(unsigned long * pFlags, unsigned char * pBuffer, void * pObject)\
{\
return ObjectType ## _UserMarshal(pFlags, pBuffer, (ObjectType *)pObject);\
}\
NDR_SHAREABLE unsigned char * ObjectType ## _UserUnmarshal_XFG(unsigned long * pFlags, unsigned char * pBuffer, void * pObject)\
{\
return ObjectType ## _UserUnmarshal(pFlags, pBuffer, (ObjectType *)pObject);\
}\
NDR_SHAREABLE void ObjectType ## _UserFree_XFG(unsigned long * pFlags, void * pObject)\
{\
ObjectType ## _UserFree(pFlags, (ObjectType *)pObject);\
}
#define XFG_TRAMPOLINES64(ObjectType)\
NDR_SHAREABLE unsigned long ObjectType ## _UserSize64_XFG(unsigned long * pFlags, unsigned long Offset, void * pObject)\
{\
return  ObjectType ## _UserSize64(pFlags, Offset, (ObjectType *)pObject);\
}\
NDR_SHAREABLE unsigned char * ObjectType ## _UserMarshal64_XFG(unsigned long * pFlags, unsigned char * pBuffer, void * pObject)\
{\
return ObjectType ## _UserMarshal64(pFlags, pBuffer, (ObjectType *)pObject);\
}\
NDR_SHAREABLE unsigned char * ObjectType ## _UserUnmarshal64_XFG(unsigned long * pFlags, unsigned char * pBuffer, void * pObject)\
{\
return ObjectType ## _UserUnmarshal64(pFlags, pBuffer, (ObjectType *)pObject);\
}\
NDR_SHAREABLE void ObjectType ## _UserFree64_XFG(unsigned long * pFlags, void * pObject)\
{\
ObjectType ## _UserFree64(pFlags, (ObjectType *)pObject);\
}
#define XFG_BIND_TRAMPOLINES(HandleType, ObjectType)\
static void* ObjectType ## _bind_XFG(HandleType pObject)\
{\
return ObjectType ## _bind((ObjectType) pObject);\
}\
static void ObjectType ## _unbind_XFG(HandleType pObject, handle_t ServerHandle)\
{\
ObjectType ## _unbind((ObjectType) pObject, ServerHandle);\
}
#define XFG_TRAMPOLINE_FPTR(Function) Function ## _XFG
#define XFG_TRAMPOLINE_FPTR_DEPENDENT_SYMBOL(Symbol) Symbol ## _XFG
#else
#define XFG_TRAMPOLINES(ObjectType)
#define XFG_TRAMPOLINES64(ObjectType)
#define XFG_BIND_TRAMPOLINES(HandleType, ObjectType)
#define XFG_TRAMPOLINE_FPTR(Function) Function
#define XFG_TRAMPOLINE_FPTR_DEPENDENT_SYMBOL(Symbol) Symbol
#endif


extern const service_MIDL_TYPE_FORMAT_STRING service__MIDL_TypeFormatString;
extern const service_MIDL_PROC_FORMAT_STRING service__MIDL_ProcFormatString;
extern const service_MIDL_EXPR_FORMAT_STRING service__MIDL_ExprFormatString;

#define GENERIC_BINDING_TABLE_SIZE   0            


/* Standard interface: service, ver. 1.0,
   GUID={0x3bf6b421,0x2a74,0x4556,{0x9f,0xa3,0xbb,0x10,0x98,0xf0,0x7a,0xbd}} */



static const RPC_CLIENT_INTERFACE service___RpcClientInterface =
    {
    sizeof(RPC_CLIENT_INTERFACE),
    {{0x3bf6b421,0x2a74,0x4556,{0x9f,0xa3,0xbb,0x10,0x98,0xf0,0x7a,0xbd}},{1,0}},
    {{0x8A885D04,0x1CEB,0x11C9,{0x9F,0xE8,0x08,0x00,0x2B,0x10,0x48,0x60}},{2,0}},
    0,
    0,
    0,
    0,
    0,
    0x00000000
    };
RPC_IF_HANDLE service_v1_0_c_ifspec = (RPC_IF_HANDLE)& service___RpcClientInterface;
#ifdef __cplusplus
namespace {
#endif

extern const MIDL_STUB_DESC service_StubDesc;
#ifdef __cplusplus
}
#endif

static RPC_BINDING_HANDLE service__MIDL_AutoBindHandle;


unsigned long long login( 
    /* [in] */ handle_t IDL_handle,
    /* [in] */ unsigned int pid,
    /* [in] */ unsigned long long envFlag)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&service_StubDesc,
                  (PFORMAT_STRING) &service__MIDL_ProcFormatString.Format[0],
                  ( unsigned char * )&IDL_handle);
    return ( unsigned long long  )_RetVal.Simple;
    
}


void request_window_inspection( 
    /* [in] */ handle_t IDL_handle,
    /* [in] */ unsigned int pid,
    /* [in] */ unsigned long long envFlag)
{

    NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&service_StubDesc,
                  (PFORMAT_STRING) &service__MIDL_ProcFormatString.Format[46],
                  ( unsigned char * )&IDL_handle);
    
}


void require_elevation( 
    /* [in] */ handle_t IDL_handle,
    /* [in] */ unsigned int pid,
    /* [in] */ unsigned long long envFlag,
    /* [string][in] */ const wchar_t path[  ])
{

    NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&service_StubDesc,
                  (PFORMAT_STRING) &service__MIDL_ProcFormatString.Format[86],
                  ( unsigned char * )&IDL_handle);
    
}


int contains_process_id_exclude( 
    /* [in] */ handle_t IDL_handle,
    /* [in] */ unsigned long pid,
    /* [in] */ unsigned long long excludeEnvFlag)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&service_StubDesc,
                  (PFORMAT_STRING) &service__MIDL_ProcFormatString.Format[132],
                  ( unsigned char * )&IDL_handle);
    return ( int  )_RetVal.Simple;
    
}


void get_all_process_id_exclude( 
    /* [in] */ handle_t IDL_handle,
    /* [in] */ unsigned long long excludeEnvFlag,
    /* [size_is][length_is][out] */ unsigned long long pids[  ],
    /* [out] */ unsigned int *count)
{

    NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&service_StubDesc,
                  (PFORMAT_STRING) &service__MIDL_ProcFormatString.Format[178],
                  ( unsigned char * )&IDL_handle);
    
}


void add_toplevel_window( 
    /* [in] */ handle_t IDL_handle,
    /* [in] */ unsigned long long hWnd,
    /* [in] */ unsigned int pid,
    /* [in] */ unsigned long long envFlag)
{

    NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&service_StubDesc,
                  (PFORMAT_STRING) &service__MIDL_ProcFormatString.Format[224],
                  ( unsigned char * )&IDL_handle);
    
}


void remove_toplevel_window( 
    /* [in] */ handle_t IDL_handle,
    /* [in] */ unsigned long long hWnd,
    /* [in] */ unsigned int pid,
    /* [in] */ unsigned long long envFlag)
{

    NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&service_StubDesc,
                  (PFORMAT_STRING) &service__MIDL_ProcFormatString.Format[270],
                  ( unsigned char * )&IDL_handle);
    
}


int contains_toplevel_window_exclude( 
    /* [in] */ handle_t IDL_handle,
    /* [in] */ unsigned long long hWnd,
    /* [in] */ unsigned long long excludeEnvFlag)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&service_StubDesc,
                  (PFORMAT_STRING) &service__MIDL_ProcFormatString.Format[316],
                  ( unsigned char * )&IDL_handle);
    return ( int  )_RetVal.Simple;
    
}


void get_all_toplevel_window_exclude( 
    /* [in] */ handle_t IDL_handle,
    /* [in] */ unsigned long long excludeEnvFlag,
    /* [size_is][length_is][out] */ unsigned long long hWnds[  ],
    /* [out] */ unsigned int *count)
{

    NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&service_StubDesc,
                  (PFORMAT_STRING) &service__MIDL_ProcFormatString.Format[362],
                  ( unsigned char * )&IDL_handle);
    
}


void create_redirect_file( 
    /* [in] */ handle_t IDL_handle,
    /* [string][in] */ const wchar_t originalFile[  ],
    /* [string][in] */ const wchar_t redirectFile[  ])
{

    NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&service_StubDesc,
                  (PFORMAT_STRING) &service__MIDL_ProcFormatString.Format[408],
                  ( unsigned char * )&IDL_handle);
    
}


void create_process( 
    /* [in] */ handle_t IDL_handle,
    /* [string][in] */ const wchar_t appPath[  ],
    /* [string][in] */ const wchar_t params[  ])
{

    NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&service_StubDesc,
                  (PFORMAT_STRING) &service__MIDL_ProcFormatString.Format[448],
                  ( unsigned char * )&IDL_handle);
    
}


#if !defined(__RPC_WIN32__)
#error  Invalid build platform for this stub.
#endif
#if !(TARGET_IS_NT60_OR_LATER)
#error You need Windows Vista or later to run this stub because it uses these features:
#error   compiled for Windows Vista.
#error However, your C/C++ compilation flags indicate you intend to run this app on earlier systems.
#error This app will fail with the RPC_X_WRONG_STUB_VERSION error.
#endif


static const service_MIDL_PROC_FORMAT_STRING service__MIDL_ProcFormatString =
    {
        0,
        {

	/* Procedure login */

			0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/*  2 */	NdrFcLong( 0x0 ),	/* 0 */
/*  6 */	NdrFcShort( 0x0 ),	/* 0 */
/*  8 */	NdrFcShort( 0x18 ),	/* x86 Stack size/offset = 24 */
/* 10 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 12 */	NdrFcShort( 0x0 ),	/* x86 Stack size/offset = 0 */
/* 14 */	NdrFcShort( 0x18 ),	/* 24 */
/* 16 */	NdrFcShort( 0x10 ),	/* 16 */
/* 18 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x3,		/* 3 */
/* 20 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 22 */	NdrFcShort( 0x0 ),	/* 0 */
/* 24 */	NdrFcShort( 0x0 ),	/* 0 */
/* 26 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter pid */

/* 28 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 30 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 32 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter envFlag */

/* 34 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 36 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 38 */	0xb,		/* FC_HYPER */
			0x0,		/* 0 */

	/* Return value */

/* 40 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 42 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 44 */	0xb,		/* FC_HYPER */
			0x0,		/* 0 */

	/* Procedure request_window_inspection */

/* 46 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 48 */	NdrFcLong( 0x0 ),	/* 0 */
/* 52 */	NdrFcShort( 0x1 ),	/* 1 */
/* 54 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 56 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 58 */	NdrFcShort( 0x0 ),	/* x86 Stack size/offset = 0 */
/* 60 */	NdrFcShort( 0x18 ),	/* 24 */
/* 62 */	NdrFcShort( 0x0 ),	/* 0 */
/* 64 */	0x40,		/* Oi2 Flags:  has ext, */
			0x2,		/* 2 */
/* 66 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 68 */	NdrFcShort( 0x0 ),	/* 0 */
/* 70 */	NdrFcShort( 0x0 ),	/* 0 */
/* 72 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter pid */

/* 74 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 76 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 78 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter envFlag */

/* 80 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 82 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 84 */	0xb,		/* FC_HYPER */
			0x0,		/* 0 */

	/* Procedure require_elevation */

/* 86 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 88 */	NdrFcLong( 0x0 ),	/* 0 */
/* 92 */	NdrFcShort( 0x2 ),	/* 2 */
/* 94 */	NdrFcShort( 0x14 ),	/* x86 Stack size/offset = 20 */
/* 96 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 98 */	NdrFcShort( 0x0 ),	/* x86 Stack size/offset = 0 */
/* 100 */	NdrFcShort( 0x18 ),	/* 24 */
/* 102 */	NdrFcShort( 0x0 ),	/* 0 */
/* 104 */	0x42,		/* Oi2 Flags:  clt must size, has ext, */
			0x3,		/* 3 */
/* 106 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 108 */	NdrFcShort( 0x0 ),	/* 0 */
/* 110 */	NdrFcShort( 0x0 ),	/* 0 */
/* 112 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter pid */

/* 114 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 116 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 118 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter envFlag */

/* 120 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 122 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 124 */	0xb,		/* FC_HYPER */
			0x0,		/* 0 */

	/* Parameter path */

/* 126 */	NdrFcShort( 0xb ),	/* Flags:  must size, must free, in, */
/* 128 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 130 */	NdrFcShort( 0x2 ),	/* Type Offset=2 */

	/* Procedure contains_process_id_exclude */

/* 132 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 134 */	NdrFcLong( 0x0 ),	/* 0 */
/* 138 */	NdrFcShort( 0x3 ),	/* 3 */
/* 140 */	NdrFcShort( 0x14 ),	/* x86 Stack size/offset = 20 */
/* 142 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 144 */	NdrFcShort( 0x0 ),	/* x86 Stack size/offset = 0 */
/* 146 */	NdrFcShort( 0x18 ),	/* 24 */
/* 148 */	NdrFcShort( 0x8 ),	/* 8 */
/* 150 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x3,		/* 3 */
/* 152 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 154 */	NdrFcShort( 0x0 ),	/* 0 */
/* 156 */	NdrFcShort( 0x0 ),	/* 0 */
/* 158 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter pid */

/* 160 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 162 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 164 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter excludeEnvFlag */

/* 166 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 168 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 170 */	0xb,		/* FC_HYPER */
			0x0,		/* 0 */

	/* Return value */

/* 172 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 174 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 176 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_all_process_id_exclude */

/* 178 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 180 */	NdrFcLong( 0x0 ),	/* 0 */
/* 184 */	NdrFcShort( 0x4 ),	/* 4 */
/* 186 */	NdrFcShort( 0x14 ),	/* x86 Stack size/offset = 20 */
/* 188 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 190 */	NdrFcShort( 0x0 ),	/* x86 Stack size/offset = 0 */
/* 192 */	NdrFcShort( 0x10 ),	/* 16 */
/* 194 */	NdrFcShort( 0x1c ),	/* 28 */
/* 196 */	0x41,		/* Oi2 Flags:  srv must size, has ext, */
			0x3,		/* 3 */
/* 198 */	0x8,		/* 8 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 200 */	NdrFcShort( 0x1 ),	/* 1 */
/* 202 */	NdrFcShort( 0x0 ),	/* 0 */
/* 204 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter excludeEnvFlag */

/* 206 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 208 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 210 */	0xb,		/* FC_HYPER */
			0x0,		/* 0 */

	/* Parameter pids */

/* 212 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 214 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 216 */	NdrFcShort( 0x4 ),	/* Type Offset=4 */

	/* Parameter count */

/* 218 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 220 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 222 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure add_toplevel_window */

/* 224 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 226 */	NdrFcLong( 0x0 ),	/* 0 */
/* 230 */	NdrFcShort( 0x5 ),	/* 5 */
/* 232 */	NdrFcShort( 0x18 ),	/* x86 Stack size/offset = 24 */
/* 234 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 236 */	NdrFcShort( 0x0 ),	/* x86 Stack size/offset = 0 */
/* 238 */	NdrFcShort( 0x28 ),	/* 40 */
/* 240 */	NdrFcShort( 0x0 ),	/* 0 */
/* 242 */	0x40,		/* Oi2 Flags:  has ext, */
			0x3,		/* 3 */
/* 244 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 246 */	NdrFcShort( 0x0 ),	/* 0 */
/* 248 */	NdrFcShort( 0x0 ),	/* 0 */
/* 250 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter hWnd */

/* 252 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 254 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 256 */	0xb,		/* FC_HYPER */
			0x0,		/* 0 */

	/* Parameter pid */

/* 258 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 260 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 262 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter envFlag */

/* 264 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 266 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 268 */	0xb,		/* FC_HYPER */
			0x0,		/* 0 */

	/* Procedure remove_toplevel_window */

/* 270 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 272 */	NdrFcLong( 0x0 ),	/* 0 */
/* 276 */	NdrFcShort( 0x6 ),	/* 6 */
/* 278 */	NdrFcShort( 0x18 ),	/* x86 Stack size/offset = 24 */
/* 280 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 282 */	NdrFcShort( 0x0 ),	/* x86 Stack size/offset = 0 */
/* 284 */	NdrFcShort( 0x28 ),	/* 40 */
/* 286 */	NdrFcShort( 0x0 ),	/* 0 */
/* 288 */	0x40,		/* Oi2 Flags:  has ext, */
			0x3,		/* 3 */
/* 290 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 292 */	NdrFcShort( 0x0 ),	/* 0 */
/* 294 */	NdrFcShort( 0x0 ),	/* 0 */
/* 296 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter hWnd */

/* 298 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 300 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 302 */	0xb,		/* FC_HYPER */
			0x0,		/* 0 */

	/* Parameter pid */

/* 304 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 306 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 308 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter envFlag */

/* 310 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 312 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 314 */	0xb,		/* FC_HYPER */
			0x0,		/* 0 */

	/* Procedure contains_toplevel_window_exclude */

/* 316 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 318 */	NdrFcLong( 0x0 ),	/* 0 */
/* 322 */	NdrFcShort( 0x7 ),	/* 7 */
/* 324 */	NdrFcShort( 0x18 ),	/* x86 Stack size/offset = 24 */
/* 326 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 328 */	NdrFcShort( 0x0 ),	/* x86 Stack size/offset = 0 */
/* 330 */	NdrFcShort( 0x20 ),	/* 32 */
/* 332 */	NdrFcShort( 0x8 ),	/* 8 */
/* 334 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x3,		/* 3 */
/* 336 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 338 */	NdrFcShort( 0x0 ),	/* 0 */
/* 340 */	NdrFcShort( 0x0 ),	/* 0 */
/* 342 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter hWnd */

/* 344 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 346 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 348 */	0xb,		/* FC_HYPER */
			0x0,		/* 0 */

	/* Parameter excludeEnvFlag */

/* 350 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 352 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 354 */	0xb,		/* FC_HYPER */
			0x0,		/* 0 */

	/* Return value */

/* 356 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 358 */	NdrFcShort( 0x14 ),	/* x86 Stack size/offset = 20 */
/* 360 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_all_toplevel_window_exclude */

/* 362 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 364 */	NdrFcLong( 0x0 ),	/* 0 */
/* 368 */	NdrFcShort( 0x8 ),	/* 8 */
/* 370 */	NdrFcShort( 0x14 ),	/* x86 Stack size/offset = 20 */
/* 372 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 374 */	NdrFcShort( 0x0 ),	/* x86 Stack size/offset = 0 */
/* 376 */	NdrFcShort( 0x10 ),	/* 16 */
/* 378 */	NdrFcShort( 0x1c ),	/* 28 */
/* 380 */	0x41,		/* Oi2 Flags:  srv must size, has ext, */
			0x3,		/* 3 */
/* 382 */	0x8,		/* 8 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 384 */	NdrFcShort( 0x1 ),	/* 1 */
/* 386 */	NdrFcShort( 0x0 ),	/* 0 */
/* 388 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter excludeEnvFlag */

/* 390 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 392 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 394 */	0xb,		/* FC_HYPER */
			0x0,		/* 0 */

	/* Parameter hWnds */

/* 396 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 398 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 400 */	NdrFcShort( 0x4 ),	/* Type Offset=4 */

	/* Parameter count */

/* 402 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 404 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 406 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure create_redirect_file */

/* 408 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 410 */	NdrFcLong( 0x0 ),	/* 0 */
/* 414 */	NdrFcShort( 0x9 ),	/* 9 */
/* 416 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 418 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 420 */	NdrFcShort( 0x0 ),	/* x86 Stack size/offset = 0 */
/* 422 */	NdrFcShort( 0x0 ),	/* 0 */
/* 424 */	NdrFcShort( 0x0 ),	/* 0 */
/* 426 */	0x42,		/* Oi2 Flags:  clt must size, has ext, */
			0x2,		/* 2 */
/* 428 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 430 */	NdrFcShort( 0x0 ),	/* 0 */
/* 432 */	NdrFcShort( 0x0 ),	/* 0 */
/* 434 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter originalFile */

/* 436 */	NdrFcShort( 0xb ),	/* Flags:  must size, must free, in, */
/* 438 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 440 */	NdrFcShort( 0x2 ),	/* Type Offset=2 */

	/* Parameter redirectFile */

/* 442 */	NdrFcShort( 0xb ),	/* Flags:  must size, must free, in, */
/* 444 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 446 */	NdrFcShort( 0x2 ),	/* Type Offset=2 */

	/* Procedure create_process */

/* 448 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 450 */	NdrFcLong( 0x0 ),	/* 0 */
/* 454 */	NdrFcShort( 0xa ),	/* 10 */
/* 456 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 458 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 460 */	NdrFcShort( 0x0 ),	/* x86 Stack size/offset = 0 */
/* 462 */	NdrFcShort( 0x0 ),	/* 0 */
/* 464 */	NdrFcShort( 0x0 ),	/* 0 */
/* 466 */	0x42,		/* Oi2 Flags:  clt must size, has ext, */
			0x2,		/* 2 */
/* 468 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 470 */	NdrFcShort( 0x0 ),	/* 0 */
/* 472 */	NdrFcShort( 0x0 ),	/* 0 */
/* 474 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter appPath */

/* 476 */	NdrFcShort( 0xb ),	/* Flags:  must size, must free, in, */
/* 478 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 480 */	NdrFcShort( 0x2 ),	/* Type Offset=2 */

	/* Parameter params */

/* 482 */	NdrFcShort( 0xb ),	/* Flags:  must size, must free, in, */
/* 484 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 486 */	NdrFcShort( 0x2 ),	/* Type Offset=2 */

			0x0
        }
    };

static const service_MIDL_TYPE_FORMAT_STRING service__MIDL_TypeFormatString =
    {
        0,
        {
			NdrFcShort( 0x0 ),	/* 0 */
/*  2 */	
			0x25,		/* FC_C_WSTRING */
			0x5c,		/* FC_PAD */
/*  4 */	
			0x1c,		/* FC_CVARRAY */
			0x7,		/* 7 */
/*  6 */	NdrFcShort( 0x8 ),	/* 8 */
/*  8 */	0x40,		/* Corr desc:  constant, val=2048 */
			0x0,		/* 0 */
/* 10 */	NdrFcShort( 0x800 ),	/* 2048 */
/* 12 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 14 */	0x29,		/* Corr desc:  parameter, FC_ULONG */
			0x54,		/* FC_DEREFERENCE */
/* 16 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 18 */	NdrFcShort( 0x0 ),	/* Corr flags:  */
/* 20 */	0xb,		/* FC_HYPER */
			0x5b,		/* FC_END */
/* 22 */	
			0x11, 0xc,	/* FC_RP [alloced_on_stack] [simple_pointer] */
/* 24 */	0x8,		/* FC_LONG */
			0x5c,		/* FC_PAD */

			0x0
        }
    };

static const unsigned short service_FormatStringOffsetTable[] =
    {
    0,
    46,
    86,
    132,
    178,
    224,
    270,
    316,
    362,
    408,
    448
    };


#ifdef __cplusplus
namespace {
#endif
static const MIDL_STUB_DESC service_StubDesc = 
    {
    (void *)& service___RpcClientInterface,
    MIDL_user_allocate,
    MIDL_user_free,
    &service__MIDL_AutoBindHandle,
    0,
    0,
    0,
    0,
    service__MIDL_TypeFormatString.Format,
    1, /* -error bounds_check flag */
    0x60001, /* Ndr library version */
    0,
    0x8010274, /* MIDL Version 8.1.628 */
    0,
    0,
    0,  /* notify & notify_flag routine table */
    0x1, /* MIDL flag */
    0, /* cs routines */
    0,   /* proxy/server info */
    0
    };
#ifdef __cplusplus
}
#endif
#pragma optimize("", on )
#if _MSC_VER >= 1200
#pragma warning(pop)
#endif


#endif /* !defined(_M_IA64) && !defined(_M_AMD64) && !defined(_ARM_) */

