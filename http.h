#ifndef HTTP
#define HTTP

#if defined(__unix) || defined(__MACH__)
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#else
#error "http.h does not have support outside of unix yet"
#endif

#ifndef HTTP_REALLOC
#include <stdlib.h>
#define HTTP_REALLOC realloc
#endif

#ifndef HTTPDEF
#define HTTPDEF
#endif

#ifdef HTTP_PTHREAD
#include <pthread.h>
#endif

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>

enum {
	HttpOk,
	HttpErrOverflow,
	HttpErrRealloc,
	HttpErrSocket,
	HttpErrSetsockopt,
	HttpErrBind,
	HttpErrListen,
	HttpErrNoDefinedMultithreading,
	HttpErrMissingRequestHandler,
	HttpErrRead,
	HttpErrOverlappingOptions,
	HTTP__ERRNO_COUNT,
};

extern int http_errno;
extern const char* http__errlist[];
extern char http__strerror_buffer[];

HTTPDEF const char* http_strerror(int http_errno);
HTTPDEF void http_perror(const char* label);

#define http__Vec(T) \
	struct { \
		size_t size, capacity; \
		typeof(T)* data; \
	}
#define http__resv(vec, n_items) do { \
	typeof(vec) v = (vec); \
	size_t n = (n_items); \
	if(v->size + (n) + 1 > ~(size_t) 0 >> 1) \
		http_errno = HttpErrOverflow; \
	else if(v->size + (n) > v->capacity) { \
		while((v->capacity = v->capacity * 2 ?: 1) < v->size + (n)); \
		if(!(v->data = HTTP_REALLOC(v->data, \
						v->capacity * sizeof(*v->data)))) \
			http_errno = HttpErrRealloc; \
	} \
} while(0)
#define http__push(vec, item) do { \
	typeof(vec) pv = (vec); \
	http__resv(pv, 1); \
	if(!http_errno) pv->data[pv->size++] = (item); \
} while(0)

typedef struct {
	char* name;
	char* value;
} HeadersEntry;

typedef http__Vec(HeadersEntry) http__HeadersEntryVec;
typedef http__Vec(http__HeadersEntryVec) Headers;

HTTPDEF uint32_t http__fnv1a_u32_hash(char* key);
HTTPDEF int hput(Headers* headers, char* name, char* value);
HTTPDEF char* hget(Headers headers, char* name);
HTTPDEF int init_headers(Headers* headers);
HTTPDEF void freehdrs(Headers headers);

typedef http__Vec(char) http_String;

typedef struct {
#if defined(__unix__) || defined(__MACH__)
	int _filedes;
#endif
	http_String data;
	char* method;
	char* path;
	char* version;
	Headers headers;
	size_t body_size;
	char* body;
} Request;

HTTPDEF void freereq(Request req);

typedef struct {
#if defined(__unix__) || defined(__MACH__)
	int _filedes;
#endif
	char* version;
	unsigned short status;
	char* message;
	Headers headers;
	bool head_written;
} Response;

typedef struct {
	HeadersEntry headers[16];
	char* message;
	char* version;
} http__rswrite_head_Options;

HTTPDEF int vrsprintf(Response res, const char* fmt, va_list va_args);
HTTPDEF int rsprintf(Response res, const char* fmt, ...);
HTTPDEF void rsend(Response res);
HTTPDEF int http_rswrite_head(Response res, unsigned short status,
		http__rswrite_head_Options opts);
HTTPDEF void freeres(Response res);

typedef struct Server {
#if defined(__unix__) || defined(__MACH__)
	int _socket;
#endif
	unsigned short port;
	void (*on_ready)(struct Server*);
	void (*on_request)(Request, Response);
	void (*on_error)(int);
	bool fork_on_request;
} Server;

typedef struct {
	unsigned backlog;
	unsigned thread_count;
	void (*on_ready)(struct Server*);
	void (*on_request)(Request, Response);
	void (*on_error)(int);
	Server** collect;
	bool fork_on_request;
} http__serve_Options;

HTTPDEF int http_serve(unsigned short port,
		http__serve_Options opts);
HTTPDEF void freesrvr(Server* server);

#ifdef HTTP_PTHREAD
HTTPDEF void* http__pthread_thread(void* void_server);
#endif
extern char http__request_buffer[];
HTTPDEF void http__wrap_on_request(Server* server);
extern bool http__default_on_error_hint;
HTTPDEF void http__default_on_error(int http_errno);

#ifdef HTTP_IMPL

int http_errno = HttpOk;

const char* http__errlist[HTTP__ERRNO_COUNT] = {
	[HttpOk] = "ok",
	[HttpErrOverflow] = "size_t overflow",
	[HttpErrRealloc] = "HTTP_REALLOC() (realloc) failed",
	[HttpErrSocket] = "socket() failed",
	[HttpErrSetsockopt] = "setsockopt() failed",
	[HttpErrBind] = "bind() failed",
	[HttpErrListen] = "listen() failed",
	[HttpErrNoDefinedMultithreading] = "no multithreading option set "
		"(eg. -DHTTP_PTHREAD for pthread)",
	[HttpErrMissingRequestHandler] = "expected on_request to be set",
	[HttpErrRead] = "read() failed",
	[HttpErrOverlappingOptions] = "two or more input options overlap",
};

char http__strerror_buffer[] =
		"unknown http errno: xxxxxxxxxxxxxxxxxxxx";

HTTPDEF const char* http_strerror(int http_errno) {
	if(http_errno > HTTP__ERRNO_COUNT || http_errno < 0) {
		snprintf(http__strerror_buffer + 20, 20, "%d", http_errno);
		return http__strerror_buffer;
	}
	return http__errlist[http_errno];
}

HTTPDEF void http_perror(const char* label) {
	fprintf(stderr, "%s: %s\n", label, http_strerror(http_errno));
}

HTTPDEF uint32_t http__fnv1a_u32_hash(char* key) {
	uint32_t hash = 2166136261u;
	while(*key) hash = (hash ^ *key++) * 16777619u;
	return hash;
}

HTTPDEF int hput(Headers* headers, char* name, char* value) {
	http__push(
			headers->data
				+ http__fnv1a_u32_hash(name) % headers->capacity, 
			((HeadersEntry) { name, value }));
	return http_errno;
}

HTTPDEF char* hget(Headers headers, char* name) {
	http__HeadersEntryVec entries
		= headers.data[http__fnv1a_u32_hash(name) % headers.capacity];
	for(size_t i = 0; i < entries.size; i++) {
		if(!strcmp(entries.data[i].name, name))
			return entries.data[i].value;
	}
	return 0;
}

HTTPDEF int init_headers(Headers* headers) {
	http__resv(headers, 16);
	if(http_errno) return http_errno;
	memset(headers->data, 0, 16 * sizeof(*headers->data));
	return 0;
}

HTTPDEF void freehdrs(Headers headers) {
	for(size_t i = 0; i < headers.capacity; i++) {
		free(headers.data[i].data);
	}
	free(headers.data);
}

HTTPDEF void freereq(Request req) {
	free(req.data.data);
	freehdrs(req.headers);
}

HTTPDEF int vrsprintf(Response res, const char* fmt, va_list va_args) {
#if defined(__unix__) || defined(__MACH__)
	return vdprintf(res._filedes, fmt, va_args);
#endif
}

HTTPDEF int rsprintf(Response res, const char* fmt, ...) {
	va_list va_args;
	va_start(va_args, fmt);
	return vrsprintf(res, fmt, va_args);
}

HTTPDEF void rsend(Response res) {
#if defined(__unix__) || defined(__MACH__)
	shutdown(res._filedes, SHUT_RDWR);
	close(res._filedes);
#endif
	freeres(res);
}

HTTPDEF int http_rswrite_head(Response res, unsigned short status,
		http__rswrite_head_Options opts) {
	rsprintf(res, "HTTP/%s %u %s\r\n",
			opts.version ?: "1.1",
			status,
			opts.message ?: "");
	
	for(size_t i = 0; i < res.headers.size; i++) {
		for(size_t j = 0; j < res.headers.data[i].size; j++) {
			rsprintf(res, "%s: %s\r\n",
					res.headers.data[i].data[j].name,
					res.headers.data[i].data[j].value);
		}
	}

	for(int i = 0; i < sizeof(opts.headers) / sizeof(*opts.headers)
			&& opts.headers[i].name; i++) {
		rsprintf(res, "%s: %s\r\n",
				opts.headers[i].name,
				opts.headers[i].value);
	}

	rsprintf(res, "\r\n");
	return 0;
}

HTTPDEF void freeres(Response res) {
	freehdrs(res.headers);
}

HTTPDEF int http_serve(unsigned short port,
		http__serve_Options opts) {
	Server* server = HTTP_REALLOC(0, sizeof(Server));
	*server = (Server) {
		.port = port,
		.on_request = opts.on_request,
		.fork_on_request = opts.fork_on_request,
	};

#if defined(__unix__) || defined(__MACH__)
	server->_socket = socket(PF_INET, SOCK_STREAM, 0);
	if(server->_socket == -1) {
		(void) HTTP_REALLOC(server, 0);
		if(opts.on_error) opts.on_error(HttpErrSocket);
		return http_errno = HttpErrSocket;
	}

	int opt = 1;
	if(setsockopt(server->_socket, SOL_SOCKET, SO_REUSEADDR,
				&opt, sizeof(opt))) {
		(void) HTTP_REALLOC(server, 0);
		if(opts.on_error) opts.on_error(HttpErrSetsockopt);
		return http_errno = HttpErrSetsockopt;
	}

	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(port),
	};
	if(bind(server->_socket, (void*) &addr, sizeof(addr))) {
		(void) HTTP_REALLOC(server, 0);
		if(opts.on_error) opts.on_error(HttpErrBind);
		return http_errno = HttpErrBind;
	}

	if(listen(server->_socket, opts.backlog ?: 10)) {
		(void) HTTP_REALLOC(server, 0);
		if(opts.on_error) opts.on_error(HttpErrListen);
		return http_errno = HttpErrListen;
	}
#endif
	if(opts.on_ready) opts.on_ready(server);
	server->on_error = opts.on_error ?: &http__default_on_error;

	if(opts.thread_count) {
		if(opts.fork_on_request) {
			(void) HTTP_REALLOC(server, 0);
			return http_errno = HttpErrOverlappingOptions;
		}

		if(!server->on_request) {
			(void) HTTP_REALLOC(server, 0);
			if(opts.on_error) opts.on_error(HttpErrMissingRequestHandler);
			return http_errno = HttpErrMissingRequestHandler;
		}
#ifdef HTTP_PTHREAD
		pthread_t tid;
		for(unsigned i = 0; i < opts.thread_count; i++) {
			pthread_create(&tid, 0, &http__pthread_thread, server);
			pthread_join(tid, 0);
		}
#else
		(void) HTTP_REALLOC(server, 0);
		if(opts.on_error) opts.on_error(HttpErrNoDefinedMultithreading);
		return http_errno = HttpErrNoDefinedMultithreading;
#endif
	} else {
		if(!server->on_request) {
			if(!opts.collect) {
				(void) HTTP_REALLOC(server, 0);
				if(opts.on_error)
					opts.on_error(HttpErrMissingRequestHandler);
				return http_errno = HttpErrMissingRequestHandler;
			}
			*opts.collect = server;
			return 0;
		}

		http__wrap_on_request(server);
	}

	(void) HTTP_REALLOC(server, 0);
	return 0;
}

HTTPDEF void freesrvr(Server* server) {
	free(server);
}

#ifdef HTTP_PTHREAD
HTTPDEF void* http__pthread_thread(void* void_server) {
	http__wrap_on_request(void_server);
	return NULL;
}
#endif

char http__request_buffer[65536];
HTTPDEF void http__wrap_on_request(Server* server) {
#if defined(__unix__) || defined(__MACH__)
	int filedes;
outer_continue:
	while((filedes = accept(server->_socket, 0, 0)) > 0) {
		if(server->fork_on_request && fork()) continue;

		Request req = { ._filedes = filedes };
		Response res = { ._filedes = filedes };

		if(init_headers(&req.headers)
				|| init_headers(&res.headers)) {
			if(req.headers.data) freehdrs(req.headers);
			shutdown(filedes, SHUT_RDWR);
			close(filedes);
			server->on_error(http_errno);
			continue;
		}

		size_t initial_bytes = read(filedes, http__request_buffer, 65536);
		if(initial_bytes == -1) {
			freehdrs(req.headers);
			freeres(res);
			server->on_error(http_errno = HttpErrRead);
			continue;
		}

		http__resv(&req.data, initial_bytes);
		if(http_errno) {
			if(http_errno != HttpErrRealloc) freereq(req);
			freeres(res);
			server->on_error(http_errno);
			continue;
		}

		memcpy(req.data.data, http__request_buffer, initial_bytes);
		if((req.data.size = initial_bytes) == 65536) {
			do {
				http__resv(&req.data, req.data.capacity);
				if(http_errno) {
					if(http_errno != HttpErrRealloc) freereq(req);
					freeres(res);
					server->on_error(http_errno);
					goto outer_continue;
				}

				size_t bytes_read = read(filedes,
						req.data.data + req.data.size,
						req.data.size);
				if(bytes_read == -1) {
					freereq(req);
					freeres(res);
					server->on_error(http_errno = HttpErrRead);
					goto outer_continue;
				}

				req.data.size += bytes_read;
			} while(req.data.size != req.data.capacity);
		}
#endif
		char* tok;
		req.method = strtok_r(req.data.data, " ", &tok);
		req.path = strtok_r(tok, " ", &tok);
		req.version = strtok_r(tok, "\r", &tok);

		for(char* header; (header = strtok_r(tok, "\r", &tok))[1];) {
			char* name = strtok_r(header, "\n:", &header);
			hput(&req.headers, name, header + 1);
			if(http_errno) {
				freereq(req);
				freeres(res);
				server->on_error(http_errno);
				goto outer_continue;
			}
		}

		req.body_size = req.data.size - (++tok - req.data.data);
		req.body = tok;
		server->on_request(req, res);
		freereq(req);

		if(server->fork_on_request) return;
	}
}

bool http__default_on_error_hint = false;
HTTPDEF void http__default_on_error(int http_errno) {
	fprintf(stderr, "\33[31;1mhttp error:\33[0m %s\n",
			http_strerror(http_errno));
	if(!http__default_on_error_hint) {
		fprintf(stderr, "\t\33[90m(this is an unreachable error, likely "
				"due to multi-threading, set the .on_error opt in "
				"serve() to edit this default message)\33[0m\n");
		http__default_on_error_hint = true;
	}
}

#endif

#define serve(port, opts...) \
	http_serve(port, (http__serve_Options) { opts })

#define rswrite_head(res, status, opts...) \
	http_rswrite_head(res, status, (http__rswrite_head_Options) { opts })

#endif
