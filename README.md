# http.h

http library written in C, support for unix-like systems

## Example

simple multi-threaded server

```c
#define HTTP_IMPL
#define HTTP_PTHREAD
#include "http.h"

#include <stdio.h>

void on_request(Request req, Response res) {
	rswrite_head(res, 200, { { "Content-Type", "text/html" } });
	rsprintf(res, "<h1>path: <code>%s</code></h1>", req.path);
	rsend(res);
}

int main() {
	serve(80,
			.thread_count = 4,
			.on_request = &on_request);
}
```
