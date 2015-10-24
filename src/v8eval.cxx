#include "v8eval.h"

#include <stdlib.h>
#include <string.h>

#include "libplatform/libplatform.h"

namespace v8eval {

static v8::Platform* platform = nullptr;

bool initialize() {
  if (platform) {
    return false;
  }

  if (!v8::V8::InitializeICU()) {
    return false;
  }

  platform = v8::platform::CreateDefaultPlatform();
  v8::V8::InitializePlatform(platform);

  return v8::V8::Initialize();
}

bool dispose() {
  if (!platform) {
    return false;
  }

  v8::V8::Dispose();

  v8::V8::ShutdownPlatform();
  delete platform;
  platform = nullptr;

  return true;
}

class ArrayBufferAllocator : public v8::ArrayBuffer::Allocator {
 public:
  virtual void* Allocate(size_t length) {
    void* data = AllocateUninitialized(length);
    return data == NULL ? data : memset(data, 0, length);
  }

  virtual void* AllocateUninitialized(size_t length) {
    return malloc(length);
  }

  virtual void Free(void* data, size_t) {
    free(data);
  }
};

static ArrayBufferAllocator allocator;

_V8::_V8() {
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = &allocator;
  isolate_ = v8::Isolate::New(create_params);

  callback_ = nullptr;
  isolate_->SetData(0, this);

  v8::Locker locker(isolate_);

  v8::Isolate::Scope isolate_scope(isolate_);
  v8::HandleScope handle_scope(isolate_);
  context_.Reset(isolate_, new_context());
}

_V8::~_V8() {
  context_.Reset();

  isolate_->Dispose();
}

v8::Local<v8::Context> _V8::new_context() {
  if (context_.IsEmpty()) {
    v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(isolate_);
    return v8::Context::New(isolate_, nullptr, global);
  } else {
    return v8::Local<v8::Context>::New(isolate_, context_);
  }
}

v8::Local<v8::String> _V8::new_string(const char* str) {
  return v8::String::NewFromUtf8(isolate_, str ? str : "", v8::NewStringType::kNormal).ToLocalChecked();
}

static std::string to_std_string(v8::Local<v8::Value> value) {
  v8::String::Utf8Value str(value);
  return *str ? *str : "Error: Cannot convert to string";
}

v8::Local<v8::Value> _V8::json_parse(v8::Local<v8::Context> context, v8::Local<v8::String> str) {
  v8::Local<v8::Object> global = context->Global();
  v8::Local<v8::Object> json = global->Get(context, new_string("JSON")).ToLocalChecked()->ToObject();
  v8::Local<v8::Function> parse = v8::Local<v8::Function>::Cast(json->Get(context, new_string("parse")).ToLocalChecked());

  v8::Local<v8::Value> result;
  v8::Local<v8::Value> value = str;
  if (!parse->Call(context, json, 1, &value).ToLocal(&result)) {
    return v8::Local<v8::Value>();  // empty
  } else {
    return result;
  }
}

v8::Local<v8::String> _V8::json_stringify(v8::Local<v8::Context> context, v8::Local<v8::Value> value) {
  v8::Local<v8::Object> global = context->Global();
  v8::Local<v8::Object> json = global->Get(context, new_string("JSON")).ToLocalChecked()->ToObject();
  v8::Local<v8::Function> stringify = v8::Local<v8::Function>::Cast(json->Get(context, new_string("stringify")).ToLocalChecked());

  v8::Local<v8::Value> result;
  if (!stringify->Call(context, json, 1, &value).ToLocal(&result)) {
    return new_string("");
  } else {
    return result->ToString();
  }
}

std::string _V8::eval(const std::string& src) {
  v8::Locker locker(isolate_);

  v8::Isolate::Scope isolate_scope(isolate_);
  v8::HandleScope handle_scope(isolate_);

  v8::Local<v8::Context> context = new_context();
  v8::Context::Scope context_scope(context);

  v8::TryCatch try_catch(isolate_);

  v8::Local<v8::String> source = new_string(src.c_str());

  v8::Local<v8::String> name = new_string("v8eval");
  v8::ScriptOrigin origin(name);

  v8::Local<v8::Script> script;
  if (!v8::Script::Compile(context, source, &origin).ToLocal(&script)) {
    return to_std_string(try_catch.Exception());
  } else {
    v8::Local<v8::Value> result;
    if (!script->Run(context).ToLocal(&result)) {
      return to_std_string(try_catch.Exception());
    } else {
      return to_std_string(json_stringify(context, result));
    }
  }
}

std::string _V8::call(const std::string& func, const std::string& args) {
  v8::Locker locker(isolate_);

  v8::Isolate::Scope isolate_scope(isolate_);
  v8::HandleScope handle_scope(isolate_);

  v8::Local<v8::Context> context = new_context();
  v8::Context::Scope context_scope(context);

  v8::TryCatch try_catch(isolate_);

  v8::Local<v8::Object> global = context->Global();
  v8::Local<v8::Value> result;
  if (!global->Get(context, new_string(func.c_str())).ToLocal(&result)) {
    return to_std_string(try_catch.Exception());
  } else if (!result->IsFunction()) {
    return "TypeError: '" + func + "' is not a function";
  }

  v8::Local<v8::Function> function = v8::Handle<v8::Function>::Cast(result);
  v8::Local<v8::Function> apply = v8::Handle<v8::Function>::Cast(function->Get(context, new_string("apply")).ToLocalChecked());
  v8::Local<v8::Value> arguments = json_parse(context, new_string(args.c_str()));
  if (arguments.IsEmpty() || !arguments->IsArray()) {
    return "TypeError: '" + args + "' is not an array";
  }

  v8::Local<v8::Value> values[] = { function, arguments };
  if (!apply->Call(context, function, 2, values).ToLocal(&result)) {
    return to_std_string(try_catch.Exception());
  } else {
    return to_std_string(json_stringify(context, result));
  }
}

void _V8::debugger_message_handler(const v8::Debug::Message& message) {
  v8::Isolate* isolate = message.GetIsolate();
  _V8 *_v8 = (v8eval::_V8*)isolate->GetData(0);
  std::string string = *v8::String::Utf8Value(message.GetJSON());

  if (_v8->callback_ != nullptr) {
    _v8->callback_(string, _v8->callbackopq_);
  }
}

bool _V8::debugger_init(debugger_cb cb, void *cbopq) {
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = &allocator;
  dbg_isolate_ = v8::Isolate::New(create_params);

  if (callback_ != nullptr) {
    return false;
  }
  callback_ = cb;
  callbackopq_ = cbopq;

  // Set Debuger callback.
  v8::Locker locker(isolate_);
  v8::Isolate::Scope isolate_scope(isolate_);
  v8::HandleScope handle_scope(isolate_);
  v8::Debug::SetMessageHandler(debugger_message_handler);

  return true;
}

void _V8::debugger_process() {
  // Process debug messages on behalf of the V8 instance.
  v8::Locker locker(isolate_);
  v8::Isolate::Scope isolate_scope(isolate_);
  v8::HandleScope handle_scope(isolate_);

  v8::Debug::ProcessDebugMessages();
}

bool _V8::debugger_send(const std::string& cmd) {
  v8::Locker locker(dbg_isolate_);
  v8::Isolate::Scope isolate_scope(dbg_isolate_);
  v8::HandleScope handle_scope(dbg_isolate_);

  if (callback_ == nullptr) {
    return false;
  }

  v8::Local<v8::String> vstr = v8::String::NewFromUtf8(dbg_isolate_, cmd.c_str(), v8::NewStringType::kNormal).ToLocalChecked();
  v8::String::Value v(vstr);
  v8::Debug::SendCommand(isolate_, *v, v.length());
  return true;
}

void _V8::debugger_stop() {
  v8::Locker locker(isolate_);
  v8::Isolate::Scope isolate_scope(isolate_);
  v8::HandleScope handle_scope(isolate_);
  v8::Debug::SetMessageHandler(nullptr);

  callback_ = nullptr;
}

//
// DbgSrv
//

// container_of helper function
//
// libuv does not accept opaque values in its callbacks, so we have to
// recover the instance of the debug server (and associated v8 vm)
// through a C++ version of offsetof().
template<class A, class B, class C>
A* container_of(B* ptr, const C A::* member) {
  size_t offset = (size_t) &(reinterpret_cast<A*>(0)->*member);
  return (A*)((char *)ptr - offset);
}

DbgSrv::DbgSrv(_V8& v8) : v8_(v8) {
  dbgsrv_port_ = 0;
  status_ = dbgsrv_offline;

  // Server Loop initialization
  uv_loop_init(&dbgsrv_loop_);

  // Debug Process Loop initialization
  uv_loop_init(&dbgproc_loop_);
  uv_async_init(&dbgproc_loop_, &dbgproc_proc_, dbgproc_do_proc_);
  uv_async_init(&dbgproc_loop_, &dbgproc_stop_, dbgproc_do_stop_);
  uv_thread_create(&dbgproc_thread_, dbgproc_, this);
}

DbgSrv::~DbgSrv() {
  v8_.debugger_stop();

  // Stop Server Loop
  uv_async_send(&dbgsrv_stop_);
  uv_thread_join(&dbgsrv_thread_);
  uv_loop_close(&dbgsrv_loop_);

  // Stop Debug Process Loop
  uv_async_send(&dbgproc_stop_);
  uv_thread_join(&dbgproc_thread_);
  uv_loop_close(&dbgproc_loop_);
}

void DbgSrv::dbgproc_do_stop_(uv_async_t *async) {
  DbgSrv *db = container_of(async, &DbgSrv::dbgproc_stop_);

  uv_close((uv_handle_t *)&db->dbgproc_proc_, NULL);
  uv_close((uv_handle_t *)&db->dbgproc_stop_, NULL);
}

void DbgSrv::dbgproc_do_proc_(uv_async_t *async) {
  DbgSrv *db = container_of(async, &DbgSrv::dbgproc_proc_);

  db->v8_.debugger_process();
}

void DbgSrv::dbgproc_(void *ptr) {
  DbgSrv *db = (DbgSrv*)ptr;

  uv_run(&db->dbgproc_loop_, UV_RUN_DEFAULT);
}

void DbgSrv::recv_from_debugger_(std::string& string, void *opq) {
  DbgSrv *db = (DbgSrv *)opq;

  db->msg_queue_.push_front(string);
  uv_async_send(&db->dbgsrv_send_);
}

void DbgSrv::send_to_socket_(uv_async_t *async) {
  DbgSrv *db = container_of(async, &DbgSrv::dbgsrv_send_);
  uv_buf_t buf;
  uv_write_t *wreq;

  while (!db->msg_queue_.empty()) {
    std::string& str = db->msg_queue_.back();

    buf = uv_buf_init((char *)str.c_str(), (unsigned int)str.size());
    wreq = (uv_write_t *)malloc(sizeof(*wreq));
    uv_write(wreq, (uv_stream_t *)&db->dbgsrv_clnt_, &buf, 1, end_write_);
    db->msg_queue_.pop_back();
  }
}

void DbgSrv::end_write_(uv_write_t *req, int status) {
  if (status) {
    fprintf(stderr, "write: %s\n", uv_strerror(status));
  }
  free(req);
}

void DbgSrv::recv_from_socket_(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
  DbgSrv *db = container_of(client, &DbgSrv::dbgsrv_clnt_);

  if (nread == 0) return;

  if (nread < 0) {
    // Close the client
    uv_close((uv_handle_t *)&db->dbgsrv_send_, NULL);
    uv_close((uv_handle_t *)&db->dbgsrv_clnt_, NULL);
    db->status_ = dbgsrv_started;
    return;
  }

  const std::string string(buf->base, nread);
  db->v8_.debugger_send(string);
  free(buf->base);

  uv_async_send(&db->dbgproc_proc_);
}

// Asynchronous processing of V8 Debugger messages
void DbgSrv::process_dbgmsgs_(uv_async_t *async) {
  DbgSrv *db = container_of(async, &DbgSrv::dbgproc_proc_);

  db->v8_.debugger_process();
}

static void alloc_buffer(uv_handle_t *handle, size_t size, uv_buf_t *buf) {
  buf->len = size;
  buf->base = (char*) malloc(size);
}

void DbgSrv::accept_(uv_stream_t *server, int status) {
  DbgSrv *db = container_of(server, &DbgSrv::dbgsrv_serv_);

  if (status < 0) {
    return;
  }

  // Connect with the client.
  uv_tcp_init(&db->dbgsrv_loop_, &db->dbgsrv_clnt_);
  if (uv_accept(server, (uv_stream_t *)&db->dbgsrv_clnt_)) {
    uv_close((uv_handle_t *)&db->dbgsrv_clnt_, NULL);
    return;
  }

  // Setup async R/W callbacks.
  uv_async_init(&db->dbgsrv_loop_, &db->dbgsrv_send_, send_to_socket_);
  uv_read_start((uv_stream_t *)&db->dbgsrv_clnt_, alloc_buffer, recv_from_socket_);

  db->status_ = dbgsrv_connected;
}

void DbgSrv::dbgsrv_(void *ptr) {
  DbgSrv *db = (DbgSrv*)ptr;

  uv_run(&db->dbgsrv_loop_, UV_RUN_DEFAULT);
}

void DbgSrv::shutdown_(uv_async_t *async) {
  DbgSrv *db = container_of(async, &DbgSrv::dbgsrv_stop_);

  // Stop Server Loop
  if (db->status_ == dbgsrv_connected) {
    uv_close((uv_handle_t *)&db->dbgsrv_send_, NULL);
    uv_close((uv_handle_t *)&db->dbgsrv_clnt_, NULL);
    db->status_ = dbgsrv_started;
  }
  if (db->status_ == dbgsrv_started) {
    uv_close((uv_handle_t *)&db->dbgsrv_serv_, NULL);
    uv_close((uv_handle_t *)&db->dbgsrv_stop_, NULL);
    db->status_ = dbgsrv_offline;
  }
}

bool DbgSrv::start(int port) {
  struct sockaddr_in addr;

  // Set up the TCP Connection.
  uv_tcp_init(&dbgsrv_loop_, &dbgsrv_serv_);
  uv_ip4_addr("127.0.0.1", port, &addr);
  if (uv_tcp_bind(&dbgsrv_serv_, (const struct sockaddr*)&addr, 0)) {
    perror("bind");
    return false;
  }

  if (port == 0) {
    int addrlen = sizeof(addr);
    if (uv_tcp_getsockname(&dbgsrv_serv_, (struct sockaddr*)&addr, &addrlen)) {
      perror("getsockname");
      return false;
    }
    dbgsrv_port_ = ntohs(addr.sin_port);
  } else {
    dbgsrv_port_ = port;
  }

  if (uv_listen((uv_stream_t *)&dbgsrv_serv_, 0, accept_)) {
    perror("listen");
    return false;
  }

  // Start V8 debugger
  v8_.debugger_init(recv_from_debugger_, this);
  status_ = dbgsrv_started;

  // Initialize shutdown async call.
  uv_async_init(&dbgsrv_loop_, &dbgsrv_stop_, shutdown_);

  uv_thread_create(&dbgsrv_thread_, dbgsrv_, this);
  return true;
}

}  // namespace v8eval
