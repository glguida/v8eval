#ifndef V8EVAL_H_
#define V8EVAL_H_

#include <string>
#include <list>

#include "uv.h"
#include "v8.h"
#include "v8-debug.h"

/// \file
namespace v8eval {

/// \brief Initialize the V8 runtime environment
/// \return success or not as boolean
///
/// This method initializes the V8 runtime environment. It must be called before creating any V8 instance.
bool initialize();

/// \brief Dispose the V8 runtime environment
/// \return success or not as boolean
///
/// This method disposes the V8 runtime environment.
bool dispose();

 typedef void (*debugger_cb)(std::string&, void *opq);

/// \class _V8
///
/// _V8 instances can be used in multiple threads.
/// But each _V8 instance can be used in only one thread at a time.
class _V8 {
 public:
  _V8();
  virtual ~_V8();

  /// \brief Evaluate JavaScript code
  /// \param src JavaScript code
  /// \return JSON-encoded result or exception message
  ///
  /// This method evaluates the given JavaScript code 'src' and returns the result in JSON.
  /// If some JavaScript exception happens in runtime, the exception message is returned.
  std::string eval(const std::string& src);

  /// \brief Call a JavaScript function
  /// \param func Name of a JavaScript function
  /// \param args JSON-encoded argument array
  /// \return JSON-encoded result or exception message
  ///
  /// This method calls the JavaScript function specified by 'func'
  /// with the JSON-encoded argument array 'args'
  /// and returns the result in JSON.
  /// If some JavaScript exception happens in runtime, the exception message is returned.
  std::string call(const std::string& func, const std::string& args);


  bool debugger_init(debugger_cb cb, void *cbopq);
  bool debugger_send(const std::string& cmd);
  void debugger_process();
  void debugger_stop();

 private:
  static void debugger_message_handler(const v8::Debug::Message& message);
  v8::Local<v8::Context> new_context();
  v8::Local<v8::String> new_string(const char* str);
  v8::Local<v8::Value> json_parse(v8::Local<v8::Context> context, v8::Local<v8::String> str);
  v8::Local<v8::String> json_stringify(v8::Local<v8::Context> context, v8::Local<v8::Value> value);

 private:
  v8::Isolate* isolate_;
  v8::Isolate* dbg_isolate_;
  v8::Persistent<v8::Context> context_;
  debugger_cb callback_;
  void *callbackopq_;
};

/// \class DbgSrv
///
/// A debugger server is associate to a _V8 instance and accepts
/// TCP/IP connections to exchange messages in the V8 debugger
/// protocol.
class DbgSrv {
 public:
  DbgSrv(_V8& v8);
  ~DbgSrv();

  /// \brief Start a debugger server
  /// \param port TCP/IP port the server will listen
  /// \return success or not as boolean
  ///
  /// The port can be set to 0 to have a port automatically assigned.
  bool start(int port);

  /// \brief Get the TCP/IP port the system is currently listening from
  /// \return A TCP/IP port or 0 if not currently set.
  inline int get_port() { return dbgsrv_port_; }

 private:
  static void recv_from_debugger_(std::string& string, void *opq);

  static void dbgsrv_do_clnt_(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf);
  static void dbgsrv_do_send_(uv_async_t *async);
  static void dbgsrv_do_serv_(uv_stream_t *server, int status);
  static void dbgsrv_do_stop_(uv_async_t *async);
  static void dbgsrv_(void *);

  static void dbgproc_do_proc_(uv_async_t *);
  static void dbgproc_do_stop_(uv_async_t *);
  static void dbgproc_(void *);

 private:
  _V8& v8_;

  enum {
    dbgsrv_offline,
    dbgsrv_started,
    dbgsrv_connected
  } status_;
  std::list<std::string> msg_queue_;

  int dbgsrv_port_;
  uv_tcp_t dbgsrv_serv_;
  uv_tcp_t dbgsrv_clnt_;
  uv_async_t dbgsrv_send_;
  uv_async_t dbgsrv_stop_;
  uv_thread_t dbgsrv_thread_;
  uv_loop_t dbgsrv_loop_;

  uv_async_t dbgproc_proc_;
  uv_async_t dbgproc_stop_;
  uv_thread_t dbgproc_thread_;
  uv_loop_t dbgproc_loop_;
};

}  // namespace v8eval

#endif  // V8EVAL_H_
