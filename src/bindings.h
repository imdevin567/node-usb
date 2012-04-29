#ifndef SRC_BINDINGS_H
#define SRC_BINDINGS_H

#include "node_usb.h"
#include "libusb.h"

// Taken from node-libmysqlclient
#define OBJUNWRAP ObjectWrap::Unwrap
#define V8STR(str) String::New(str)
#define V8SYM(str) String::NewSymbol(str)

#ifdef ENABLE_DEBUG
  #define DEBUG_HEADER fprintf(stderr, "node-usb [%s:%s() %d]: ", __FILE__, __FUNCTION__, __LINE__); 
  #define DEBUG_FOOTER fprintf(stderr, "\n");
  #define DEBUG(STRING) DEBUG_HEADER fprintf(stderr, "%s", STRING); DEBUG_FOOTER
  #define DEBUG_OPT(...) DEBUG_HEADER fprintf(stderr, __VA_ARGS__); DEBUG_FOOTER
  #define DUMP_BYTE_STREAM(STREAM, LENGTH) DEBUG_HEADER for (int i = 0; i < LENGTH; i++) { fprintf(stderr, "0x%02X ", STREAM[i]); } DEBUG_FOOTER
#else
  #define DEBUG(str)
  #define DEBUG_OPT(...)
  #define DUMP_BYTE_STREAM(STREAM, LENGTH)
#endif

#define THROW_BAD_ARGS(FAIL_MSG) return ThrowException(Exception::TypeError(V8STR(FAIL_MSG)));
#define THROW_ERROR(FAIL_MSG) return ThrowException(Exception::Error(V8STR(FAIL_MSG))));
#define THROW_NOT_YET return ThrowException(Exception::Error(String::Concat(String::New(__FUNCTION__), String::New("not yet supported"))));
#define CREATE_ERROR_OBJECT_AND_CLOSE_SCOPE(ERRNO, scope) \
		Local<Object> error = Object::New();\
		error->Set(V8SYM("errno"), Integer::New(ERRNO));\
		error->Set(V8SYM("error"), errno_exception(ERRNO));\
		return scope.Close(error);\

#define CHECK_USB(r, scope) \
	if (r < LIBUSB_SUCCESS) { \
		CREATE_ERROR_OBJECT_AND_CLOSE_SCOPE(r, scope); \
	}

#define LOCAL(TYPE, VARNAME, REF) \
		HandleScope scope;\
		TYPE *VARNAME = OBJUNWRAP<TYPE>(REF);
		
#define EIO_CUSTOM(FUNC, STRUCTURE, CALLBACK) \
		uv_work_t* req = new uv_work_t();\
		req->data = STRUCTURE;\
		uv_queue_work(uv_default_loop(), req, FUNC, CALLBACK);\
		uv_ref(uv_default_loop());

#define	EIO_CAST(TYPE, VARNAME) struct TYPE *VARNAME = reinterpret_cast<struct TYPE *>(req->data);
#define	EIO_NEW(TYPE, VARNAME) \
		struct TYPE *VARNAME = (struct TYPE *) calloc(1, sizeof(struct TYPE)); \
		if (!VARNAME) { \
			V8::LowMemoryNotification(); \
		}

#define EIO_DELEGATION(VARNAME, CALLBACK_ARG_IDX) \
		Local<Function> callback; \
		if (args.Length() > (CALLBACK_ARG_IDX)) { \
			if (!args[CALLBACK_ARG_IDX]->IsFunction()) { \
				return ThrowException(Exception::TypeError( String::New("Argument " #CALLBACK_ARG_IDX " must be a function"))); \
			} \
			callback = Local<Function>::Cast(args[CALLBACK_ARG_IDX]); \
		} \
		VARNAME->callback = Persistent<Function>::New(callback);

#define EIO_HANDLE_ERROR(VARNAME) \
			if(VARNAME->errsource) { \
				Handle<Object> error = Object::New(); \
				error->Set(V8SYM("error_source"), V8STR(VARNAME->errsource)); \
				error->Set(V8SYM("error_code"), Uint32::New(VARNAME->errcode)); \
				Local<Value> argv[1]; \
				argv[0] = Local<Value>::New(scope.Close(error)); \
				TryCatch try_catch; \
				VARNAME->callback->Call(Context::GetCurrent()->Global(), 1, argv); \
				if (try_catch.HasCaught()) { \
					FatalException(try_catch); \
				} \
			}

#define EIO_AFTER(VARNAME, SELF) \
		uv_unref(uv_default_loop()); \
		if (!VARNAME->callback.IsEmpty()) { \
			HandleScope scope; \
			EIO_HANDLE_ERROR(VARNAME) \
			VARNAME->callback.Dispose(); \
		}\
		delete req;\
		VARNAME->SELF->Unref();
		
#define TRANSFER_REQUEST_FREE(STRUCT, SELF)\
		EIO_CAST(STRUCT, transfer_req)\
		EIO_AFTER(transfer_req, SELF)\
		free(transfer_req);

#define TRANSFER_REQUEST_FREE_WITH_DATA(STRUCT, SELF)\
		EIO_CAST(STRUCT, transfer_req)\
		uv_unref(uv_default_loop()); \
		if (!transfer_req->callback.IsEmpty()) { \
			HandleScope scope; \
			EIO_HANDLE_ERROR(transfer_req) \
			if (!transfer_req->errsource) {\
				Buffer *result = Buffer::New((char *)transfer_req->data, transfer_req->bytesTransferred); \
				Local<Value> argv[1]; \
				argv[0] = Local<Value>::New(result->handle_); \
				TryCatch try_catch; \
				transfer_req->callback->Call(Context::GetCurrent()->Global(), 1, argv); \
				if (try_catch.HasCaught()) { \
					FatalException(try_catch); \
				} \
			}\
			transfer_req->callback.Dispose(); \
		}\
		delete req;\
		transfer_req->SELF->Unref();\
		free(transfer_req);\

#define INIT_TRANSFER_CALL(MINIMUM_ARG_LENGTH, CALLBACK_ARG_IDX, TIMEOUT_ARG_IDX) \
		libusb_endpoint_direction modus; \
		uint32_t timeout = 0; \
		int32_t buflen = 0; \
		unsigned char *buf; \
\
		if (args.Length() < MINIMUM_ARG_LENGTH || !args[CALLBACK_ARG_IDX]->IsFunction()) { \
			THROW_BAD_ARGS("Endpoint::Transfer missing arguments!!") \
		} \
		if (Buffer::HasInstance(args[0])) { \
			modus = LIBUSB_ENDPOINT_OUT; \
		} else { \
			modus = LIBUSB_ENDPOINT_IN; \
			if (!args[0]->IsUint32()) { \
			      THROW_BAD_ARGS("Endpoint::Transfer in READ mode expects uint32_t as first parameter. If you want to write something, you must provide a Buffer instance as first parameter") \
			} \
		} \
		if (modus == LIBUSB_ENDPOINT_OUT) {\
		  	Local<Object> _buffer = args[0]->ToObject();\
			buflen = Buffer::Length(_buffer); \
			buf = reinterpret_cast<unsigned char*>(Buffer::Data(_buffer));\
			DEBUG("Dumping byte stream...")\
			DUMP_BYTE_STREAM(buf, buflen);\
		}\
		else {\
			buflen = args[0]->Uint32Value();\
			buf = new unsigned char[buflen];\
		}\
		if (args.Length() >= (TIMEOUT_ARG_IDX + 1)) {\
			if (!args[TIMEOUT_ARG_IDX]->IsUint32()) {\
				THROW_BAD_ARGS("Endpoint::Transfer expects unsigned int as timeout parameter")\
			} else {\
				timeout = args[TIMEOUT_ARG_IDX]->Uint32Value();\
			}\
		}

namespace NodeUsb  {
	class Device;

	struct nodeusb_endpoint_selection {
		int interface_number;
		int interface_alternate_setting;
		int endpoint_number;
	};	

	struct request {
		Persistent<Function> callback;
		int errcode;
		const char * errsource;
	};

	struct nodeusb_transfer:request {
		unsigned char *data;
		unsigned int timeout;
		uint16_t bytesTransferred;
	};


	static inline Local<Value> errno_exception(int errorno) {
		Local<Value> e  = Exception::Error(String::NewSymbol(strerror(errorno)));
		Local<Object> obj = e->ToObject();
		std::string err = "";

		obj->Set(NODE_PSYMBOL("errno"), Integer::New(errorno));
		// taken from pyusb
		switch (errorno) {
			case LIBUSB_ERROR_IO:
				err = "Input/output error";
				break;
			case LIBUSB_ERROR_INVALID_PARAM:
				err  = "Invalid parameter";
				break;
			case LIBUSB_ERROR_ACCESS:
				err  = "Access denied (insufficient permissions)";
				break;
			case LIBUSB_ERROR_NO_DEVICE:
				err = "No such device (it may have been disconnected)";
				break;
			case LIBUSB_ERROR_NOT_FOUND:
				err = "Entity not found";
				break;
			case LIBUSB_ERROR_BUSY:
				err = "Resource busy";
				break;
			case LIBUSB_ERROR_TIMEOUT:
				err = "Operation timed out";
				break;
			case LIBUSB_ERROR_OVERFLOW:
				err = "Overflow";
				break;
			case LIBUSB_ERROR_PIPE:
				err = "Pipe error";
				break;
			case LIBUSB_ERROR_INTERRUPTED:
				err = "System call interrupted (perhaps due to signal)";
				break;
			case LIBUSB_ERROR_NO_MEM:
				err = "Insufficient memory";
				break;
			case LIBUSB_ERROR_NOT_SUPPORTED:
				err = "Operation not supported or unimplemented on this platform";
				break;
			default:
				err = "Unknown error";
				break;
		}

		// convert err to const char* with help of c_str()
		obj->Set(NODE_PSYMBOL("msg"), String::New(err.c_str()));
		return e;
	}
}
#endif
