# HTTP

HTTP client and server functionality for web requests.

```xxml
#import Language::Core;
#import Language::Network;
```

**Note:** HTTP functionality requires C++ runtime backing and may need external libraries (e.g., libcurl) for full implementation.

---

## HTTPResponse

Represents an HTTP response.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `statusCode` | `Integer^` | HTTP status code |
| `body` | `String^` | Response body |

### Constructor

```xxml
Instantiate Network::HTTPResponse^ As <response> = Network::HTTPResponse::Constructor();
```

### Methods

#### getStatusCode

Get the HTTP status code.

```xxml
Instantiate Integer^ As <code> = response.getStatusCode();
```

| Method | Returns | Description |
|--------|---------|-------------|
| `getStatusCode()` | `Integer^` | Status code (200, 404, etc.) |

#### getBody

Get the response body.

```xxml
Instantiate String^ As <content> = response.getBody();
```

| Method | Returns | Description |
|--------|---------|-------------|
| `getBody()` | `String^` | Response content |

#### isSuccess

Check if request was successful (2xx status).

```xxml
If (response.isSuccess()) -> {
    Run Console::printLine(String::Constructor("Request succeeded"));
}
```

| Method | Returns | Description |
|--------|---------|-------------|
| `isSuccess()` | `Bool^` | True for 200-299 |

#### isError

Check if request returned an error (4xx or 5xx status).

```xxml
If (response.isError()) -> {
    Run Console::printError(String::Constructor("Request failed"));
}
```

| Method | Returns | Description |
|--------|---------|-------------|
| `isError()` | `Bool^` | True for 400+ |

---

## HTTPClient

HTTP client for making web requests.

### Constructor

```xxml
Instantiate Network::HTTPClient^ As <client> = Network::HTTPClient::Constructor();
```

| Constructor | Description |
|-------------|-------------|
| `Constructor()` | Create HTTP client |

### Configuration

#### setHeader

Set a request header.

```xxml
Run client.setHeader(String::Constructor("Content-Type"), String::Constructor("application/json"));
Run client.setHeader(String::Constructor("Authorization"), String::Constructor("Bearer token123"));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `setHeader(key: String^, value: String^)` | `None` | Set header |

#### setTimeout

Set request timeout.

```xxml
Run client.setTimeout(Integer::Constructor(5000));  // 5 seconds
```

| Method | Returns | Description |
|--------|---------|-------------|
| `setTimeout(milliseconds: Integer^)` | `None` | Set timeout |

---

### HTTP Methods

#### performGet

Perform a GET request.

```xxml
Instantiate Network::HTTPResponse^ As <response> = client.performGet(
    String::Constructor("https://api.example.com/data")
);
```

| Method | Returns | Description |
|--------|---------|-------------|
| `performGet(url: String^)` | `HTTPResponse^` | GET response |

#### performPost

Perform a POST request with data.

```xxml
Instantiate Network::HTTPResponse^ As <response> = client.performPost(
    String::Constructor("https://api.example.com/submit"),
    String::Constructor("{\"name\":\"Alice\"}")
);
```

| Method | Returns | Description |
|--------|---------|-------------|
| `performPost(url: String^, data: String^)` | `HTTPResponse^` | POST response |

---

## HTTPServer

Simple HTTP server (placeholder for future implementation).

### Constructor

```xxml
Instantiate Network::HTTPServer^ As <server> = Network::HTTPServer::Constructor();
```

| Constructor | Description |
|-------------|-------------|
| `Constructor()` | Create HTTP server |

### Server Control

#### listen

Start listening on a port.

```xxml
Run server.listen(Integer::Constructor(8080));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `listen(port: Integer^)` | `None` | Start server |

#### stop

Stop the server.

```xxml
Run server.stop();
```

| Method | Returns | Description |
|--------|---------|-------------|
| `stop()` | `None` | Stop server |

#### isRunning

Check if server is running.

```xxml
If (server.isRunning()) -> {
    Run Console::printLine(String::Constructor("Server is running"));
}
```

| Method | Returns | Description |
|--------|---------|-------------|
| `isRunning()` | `Bool^` | True if running |

---

## Complete Example

```xxml
#import Language::Core;
#import Language::Network;
#import Language::Format;
#import Language::System;

[ Entrypoint
    {
        // Create HTTP client
        Instantiate Network::HTTPClient^ As <client> = Network::HTTPClient::Constructor();

        // Configure client
        Run client.setHeader(String::Constructor("Accept"), String::Constructor("application/json"));
        Run client.setTimeout(Integer::Constructor(10000));  // 10 second timeout

        // Make GET request
        Run Console::printLine(String::Constructor("Making GET request..."));
        Instantiate Network::HTTPResponse^ As <getResp> = client.performGet(
            String::Constructor("https://api.example.com/users/1")
        );

        If (getResp.isSuccess()) -> {
            Run Console::printLine(String::Constructor("GET succeeded"));
            Run Console::printLine(String::Constructor("Body: ").append(getResp.getBody()));
        }

        // Make POST request with JSON
        Instantiate Format::JSONObject^ As <postData> = Format::JSONObject::Constructor();
        Run postData.set(String::Constructor("name"), String::Constructor("Alice"));
        Run postData.set(String::Constructor("email"), String::Constructor("alice@example.com"));

        Run client.setHeader(String::Constructor("Content-Type"), String::Constructor("application/json"));

        Instantiate Network::HTTPResponse^ As <postResp> = client.performPost(
            String::Constructor("https://api.example.com/users"),
            postData.stringify()
        );

        If (postResp.isSuccess()) -> {
            Run Console::printLine(String::Constructor("POST succeeded"));
            Run Console::printLine(String::Constructor("Status: ").append(postResp.getStatusCode().toString()));
        }

        If (postResp.isError()) -> {
            Run Console::printError(String::Constructor("POST failed with status: ").append(postResp.getStatusCode().toString()));
        }

        Exit(0);
    }
]
```

---

## Status Code Ranges

| Range | Category | Example |
|-------|----------|---------|
| 200-299 | Success | 200 OK, 201 Created |
| 300-399 | Redirect | 301 Moved, 304 Not Modified |
| 400-499 | Client Error | 400 Bad Request, 404 Not Found |
| 500-599 | Server Error | 500 Internal Error, 503 Unavailable |

---

## See Also

- [JSON](JSON.md) - JSON parsing for API responses
- [Console](CONSOLE.md) - Console output
- [Core Types](CORE.md) - String, Integer, Bool

