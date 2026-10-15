/*
    Code by blackb0x @ GBAtemp.net
    This allows the Wii to download from servers that use SNI.
*/
#include <network.h>
#include <ogc/lwp_watchdog.h>

#include "../version.h"
#include "gecko.h"
#include "https.h"
#include "prompts/ProgressWindow.h"
#include "settings/ProxySettings.h"
#include "utils/base64.h"

u8 loop;
WOLFSSL_SESSION *session;

int https_write(HTTP_INFO *httpinfo, char *buffer, int len, bool proxy)
{
    int ret, pos = 0;
    int rlen = len > BLOCK_SIZE ? BLOCK_SIZE : len;
    u64 time = gettime();
    while (ticks_to_millisecs(diff_ticks(time, gettime())) < READ_WRITE_TIMEOUT)
    {
        if (httpinfo->use_https && !proxy)
            ret = wolfSSL_write(httpinfo->ssl, &buffer[pos], rlen);
        else
            ret = net_write(httpinfo->sock, &buffer[pos], rlen);
        if (ret > 0)
        {
            pos += ret;
            rlen = len - pos > BLOCK_SIZE ? BLOCK_SIZE : len - pos;
            if (pos >= len)
                return pos;
            time = gettime();
        }
        usleep(10000);
    }
#ifdef DEBUG_NETWORK
    gprintf("The connection timed out (write)\n");
#endif
    return -ETIMEDOUT;
}

int https_read(HTTP_INFO *httpinfo, char *buffer, int len, bool proxy)
{
    int ret = -ETIMEDOUT;
    u64 time = gettime();
    if (len > BLOCK_SIZE)
        len = BLOCK_SIZE;
    while (ticks_to_millisecs(diff_ticks(time, gettime())) < READ_WRITE_TIMEOUT)
    {
        if (httpinfo->use_https && !proxy)
            ret = wolfSSL_read(httpinfo->ssl, buffer, len);
        else
            ret = net_read(httpinfo->sock, buffer, len);
        if (ret >= 0)
            return ret;
        usleep(10000);
    }
#ifdef DEBUG_NETWORK
    gprintf("The connection timed out (read)\n");
#endif
    return -ETIMEDOUT;
}

int send_callback(__attribute__((unused)) WOLFSSL *ssl, char *buf, int sz, void *ctx)
{
    int sent = net_write(*(int *)ctx, buf, sz);
    if (sent < 0)
    {
        if (sent == -EAGAIN)
            return WOLFSSL_CBIO_ERR_WANT_WRITE;
        else if (sent == -ECONNRESET)
            return WOLFSSL_CBIO_ERR_CONN_RST;
        else if (sent == -EINTR)
            return WOLFSSL_CBIO_ERR_ISR;
        else if (sent == -EPIPE)
            return WOLFSSL_CBIO_ERR_CONN_CLOSE;
        else
            return WOLFSSL_CBIO_ERR_GENERAL;
    }
    return sent;
}

int recv_callback(__attribute__((unused)) WOLFSSL *ssl, char *buf, int sz, void *ctx)
{
    int recvd = net_read(*(int *)ctx, buf, sz);
    if (recvd < 0)
    {
        if (recvd == -EAGAIN)
            return WOLFSSL_CBIO_ERR_WANT_READ;
        else if (recvd == -ECONNRESET)
            return WOLFSSL_CBIO_ERR_CONN_RST;
        else if (recvd == -EINTR)
            return WOLFSSL_CBIO_ERR_ISR;
        else if (recvd == -ECONNABORTED)
            return WOLFSSL_CBIO_ERR_CONN_CLOSE;
        else
            return WOLFSSL_CBIO_ERR_GENERAL;
    }
    else if (recvd == 0)
        return WOLFSSL_CBIO_ERR_CONN_CLOSE;
    return recvd;
}

void https_close(HTTP_INFO *httpinfo)
{
    if (httpinfo->use_https)
    {
        wolfSSL_shutdown(httpinfo->ssl);
        wolfSSL_free(httpinfo->ssl);
        wolfSSL_CTX_free(httpinfo->ctx);
    }
    net_close(httpinfo->sock);
#ifdef DEBUG_NETWORK
    gprintf("Closed socket and cleaned up\n");
#endif
}

// The name and the value both come off the network. value_len is whatever the
// server sent, the value is not terminated, and dst is a fixed buffer, so the
// copy needs the size of dst and cannot use strlcpy, which reads to a NUL.
bool get_header_value(struct phr_header *headers, size_t num_headers, char *dst, size_t dst_size, char *header)
{
    size_t header_len = strlen(header);
    for (size_t i = 0; i != num_headers; ++i)
    {
        // Without the length test a header named "t" matches "transfer-encoding"
        if (headers[i].name_len == header_len && strncasecmp(header, headers[i].name, header_len) == 0)
        {
            size_t len = headers[i].value_len;
            if (len >= dst_size)
                len = dst_size - 1;
            memcpy(dst, headers[i].value, len);
            dst[len] = '\0';
            return true;
        }
    }
    return false;
}

u64 get_header_value_int(struct phr_header *headers, size_t num_headers, char *header)
{
    char header_value[30];
    if (!get_header_value(headers, num_headers, header_value, sizeof(header_value), header))
        return 0;
    return strtoull(header_value, NULL, 0);
}

bool is_chunked(struct phr_header *headers, size_t num_headers)
{
    char encoding[32];
    if (!get_header_value(headers, num_headers, encoding, sizeof(encoding), "transfer-encoding"))
        return false;
    return (strcasecmp(encoding, "chunked") == 0);
}

// Several callers read the download as text, so keep a terminator after the end.
// A shrinking realloc that fails must not be reported as a finished download.
static bool finish_download(struct download *buffer, size_t size)
{
    // The body is in the file; data was only the block it passed through
    if (buffer->file)
    {
        buffer->size = size;
        return true;
    }
    char *shrunk = MEM2_realloc(buffer->data, size + 1);
    if (!shrunk)
        return false;
    buffer->data = shrunk;
    buffer->data[size] = '\0';
    buffer->size = size;
    return true;
}

// Where the next body bytes go. In memory that is the end of the body, which
// grows as it fills. For a file it is the start of one fixed block, which
// body_commit() writes out before the next read, so a body of any size needs
// no more memory than that block.
static char *body_window(struct download *buffer, size_t *capacity, size_t received, size_t *room)
{
    if (buffer->file)
    {
        *room = *capacity;
        return buffer->data;
    }
    if (received == *capacity)
    {
#ifdef DEBUG_NETWORK
        gprintf("Increased buffer size\n");
#endif
        // realloc leaves the old block allocated when it fails
        char *grown = MEM2_realloc(buffer->data, *capacity * 2 + 1);
        if (!grown) // A custom theme is using too much memory
        {
#ifdef DEBUG_NETWORK
            gprintf("Out of memory!\n");
#endif
            errno = ENOMEM;
            return NULL;
        }
        buffer->data = grown;
        *capacity *= 2;
    }
    *room = *capacity - received;
    return &buffer->data[received];
}

// Takes len body bytes at p, the window body_window() handed out
static bool body_commit(struct download *buffer, const char *p, size_t len, size_t *received)
{
    // A short write is a full card: the download fails, not the file
    if (buffer->file && len > 0 && fwrite(p, 1, len, buffer->file) != len)
        return false;
    *received += len;
    return !(buffer->max_size && *received > buffer->max_size);
}

// pending: body bytes that came with the headers, already at the window's start
bool read_chunked(HTTP_INFO *httpinfo, struct download *buffer, size_t capacity, size_t pending)
{
    struct phr_chunked_decoder decoder = {0};
    size_t received = 0, rsize, room;
    ssize_t pret;
    char *window;
    int ret;
    decoder.consume_trailer = true;
#ifdef DEBUG_NETWORK
    gprintf("Data is chunked\n");
#endif
    do
    {
        if (buffer->show_progress)
        {
            if (ProgressCanceled())
                return false;
            ShowProgress(received, received * 2 + BLOCK_SIZE); // Unknown size for chunked transfers
        }
        if (!(window = body_window(buffer, &capacity, received, &room)))
            return false;
        if (pending)
        {
            ret = pending;
            pending = 0;
        }
        else if ((ret = https_read(httpinfo, window, room, false)) < 1)
            return false;
        rsize = ret;
        pret = phr_decode_chunked(&decoder, window, &rsize);
        if (pret == -1)
        {
#ifdef DEBUG_NETWORK
            gprintf("Parse error\n");
#endif
            return false;
        }
        if (!body_commit(buffer, window, rsize, &received))
            return false;
    } while (pret == -2);
    return finish_download(buffer, received);
}

// pending: body bytes that came with the headers, already at the window's start
bool read_all(HTTP_INFO *httpinfo, struct download *buffer, size_t capacity, size_t pending)
{
    size_t received = 0, room;
    char *window;
    int ret;
#ifdef DEBUG_NETWORK
    gprintf("Data is not chunked\n");
#endif
    while (true)
    {
        if (buffer->show_progress)
        {
            if (ProgressCanceled())
                return false;
            ShowProgress(received, buffer->content_length);
        }
        // Done once the body the server announced is in
        if (buffer->content_length > 0 && received == buffer->content_length)
            break;
        if (!(window = body_window(buffer, &capacity, received, &room)))
            return false;
        // Never read past the announced end
        if (buffer->content_length > 0 && room > buffer->content_length - received)
            room = buffer->content_length - received;
        if (pending)
        {
            ret = pending;
            pending = 0;
        }
        else if ((ret = https_read(httpinfo, window, room, false)) == 0)
            break;
        if (ret < 0)
            return false;
        if (!body_commit(buffer, window, ret, &received))
            return false;
        // More than the server announced
        if (buffer->content_length > 0 && received > buffer->content_length)
            return false;
    };
    if (!finish_download(buffer, received))
        return false;
    return (buffer->content_length > 0 && buffer->content_length == received);
}

bool get_response(HTTP_INFO *httpinfo, HTTP_RESPONSE *resp, bool proxy)
{
    int rret, minor_version;
    size_t msg_len, prevbuflen;
    const char *msg;

    while (true)
    {
        if ((rret = https_read(httpinfo, &resp->data[resp->buflen], 1, proxy)) < 1)
            return false;
        prevbuflen = resp->buflen;
        resp->buflen += rret;
        // Parse the response
        resp->num_headers = sizeof(resp->headers) / sizeof(resp->headers[0]);
        if ((resp->pret = phr_parse_response(resp->data, resp->buflen, &minor_version, &resp->status, &msg, &msg_len,
                                             resp->headers, &resp->num_headers, prevbuflen)) > 0)
            return true;
        else if (resp->pret == -1)
        {
#ifdef DEBUG_NETWORK
            gprintf("pret error %i\n", resp->pret);
#endif
            return false;
        }
        if (resp->buflen == sizeof(resp->data))
        {
#ifdef DEBUG_NETWORK
            gprintf("buflen error %lu\n", (unsigned long)resp->buflen);
#endif
            return false;
        }
    }
    return false;
}

bool check_ip(char *str)
{
    int partA, partB, partC, partD;
    char extra;
    // We avoid using regex because it increases the file size
    return (sscanf(str, "%d.%d.%d.%d%c", &partA, &partB, &partC, &partD, &extra) == 4);
}

bool connect_proxy(HTTP_INFO *httpinfo, char *host, char *username, char *password)
{
    char request[500];
    char credentials[66];
    char *auth;
    int len;
    if (username && password)
    {
        if (!snprintf(credentials, sizeof(credentials), "%s:%s", username, password))
            return false;
        if (!(auth = base64(credentials, strlen(credentials), &len)))
            return false;
        len = snprintf(request, sizeof(request),
                       "CONNECT %s:%i HTTP/1.1\r\nProxy-Authorization: Basic %s\r\nUser-Agent: curl/8.12.1\r\n\r\n",
                       host, httpinfo->use_https ? 443 : 80, auth);
        MEM2_free(auth);
    }
    else
        len = snprintf(request, sizeof(request),
                       "CONNECT %s:%i HTTP/1.1\r\nUser-Agent: curl/8.12.1\r\n\r\n",
                       host, httpinfo->use_https ? 443 : 80);
    if (len > 0 && https_write(httpinfo, request, len, true) != len)
        return false;
    // On the heap: HTTP_RESPONSE holds 16 KB of headers
    HTTP_RESPONSE *response = (HTTP_RESPONSE *)MEM2_alloc(sizeof(HTTP_RESPONSE));
    if (!response)
        return false;
    memset(response, 0, sizeof(*response));
    bool ok = get_response(httpinfo, response, true) && response->status == 200;
    MEM2_free(response);
    return ok;
}

static int tcp_connect(char *host, u16 port)
{
    struct sockaddr_in sin;
    s32 sock, ret;
    u32 ipaddress;
    u64 time;
#ifdef DEBUG_NETWORK
    gprintf("Connecting to %s", host);
#endif
    if ((ipaddress = check_ip(host) ? inet_addr(host) : getipbynamecached(host)) == 0)
        return -EFAULT;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = ipaddress;
#ifdef DEBUG_NETWORK
    if (!check_ip(host))
        gprintf(" (%s)", inet_ntoa(sin.sin_addr));
#endif
    if ((sock = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) < 0)
        return sock;
    net_fcntl(sock, F_SETFL, 4);
    time = gettime();
    while (ticks_to_millisecs(diff_ticks(time, gettime())) < CONNECT_TIMEOUT)
    {
        if ((ret = net_connect(sock, (struct sockaddr *)&sin, sizeof(sin))) < 0)
        {
            if (ret == -EISCONN)
                return sock;
            if (ret == -EINPROGRESS || ret == -EALREADY)
            {
                usleep(10000);
                continue;
            }
            net_close(sock);
            return ret;
        }
    }
    net_close(sock);
    return -ETIMEDOUT;
}

// What downloadfile() does with the response once its headers are in.
static void handle_response(HTTP_INFO *httpinfo, HTTP_RESPONSE *response, struct download *buffer, const char *url)
{
    // The website wants to redirect us
    if (response->status == 301 || response->status == 302 || response->status == 303 ||
        response->status == 307 || response->status == 308)
    {
        https_close(httpinfo);
        if (loop == REDIRECT_LIMIT)
        {
#ifdef DEBUG_NETWORK
            gprintf("Reached redirect limit\n");
#endif
            return;
        }
        loop++;
        char location[2049];
        if (!get_header_value(response->headers, response->num_headers, location, sizeof(location), "location"))
            return;
#ifdef DEBUG_NETWORK
        gprintf("Redirect #%i - %s\n", loop, location);
#endif
        downloadfile(location, buffer);
        return;
    }
    // It's not a redirect, so reset the loop
    loop = 0;
    // Exit if it's a GameTDB HEAD request
    if (buffer->gametdbcheck)
    {
        buffer->gametdbcheck = get_header_value_int(response->headers, response->num_headers, "x-gametdb-timestamp");
        https_close(httpinfo);
        return;
    }
    // We got what we wanted
    if (response->status == 200)
    {
        // Body bytes that came with the headers; get_response() reads the
        // headers a byte at a time, so normally none
        size_t first = response->buflen - response->pret;
        // In memory the body starts in this block and grows from it. To a file
        // every read passes through it and it never grows.
        size_t block = buffer->file ? BLOCK_SIZE : 4096;
        size_t capacity = first > block ? first : block;
        buffer->data = MEM2_alloc(capacity + 1);
        if (!buffer->data)
        {
            https_close(httpinfo);
            return;
        }
        memcpy(buffer->data, &response->data[response->pret], first);
        // Determine how to read the data
        bool dl_valid;
        if (buffer->max_size && first > buffer->max_size)
            dl_valid = false;
        else if (is_chunked(response->headers, response->num_headers))
            dl_valid = read_chunked(httpinfo, buffer, capacity, first);
        else
        {
            buffer->content_length = get_header_value_int(response->headers, response->num_headers, "content-length");
            // The server says up front that it will send too much
            if (buffer->max_size && buffer->content_length > buffer->max_size)
                dl_valid = false;
            else
                dl_valid = read_all(httpinfo, buffer, capacity, first);
        }
        // The caller of a file download gets the file, never the block
        if (buffer->file)
        {
            MEM2_free(buffer->data);
            buffer->data = NULL;
        }
        // Check if the download is incomplete
        if (!dl_valid || buffer->size < 1)
        {
            buffer->size = 0;
            if (buffer->data)
                MEM2_free(buffer->data);
            // A caller may download into the same struct again, which frees data
            buffer->data = NULL;
#ifdef DEBUG_NETWORK
            gprintf("Removed incomplete download\n");
#endif
            https_close(httpinfo);
            return;
        }
        // Finished
        https_close(httpinfo);
#ifdef DEBUG_NETWORK
        gprintf("Download size: %llu\n", (long long)buffer->size);
        gprintf("------------- HEADERS -------------\n");
        for (size_t i = 0; i != response->num_headers; ++i)
            gprintf("%.*s: %.*s\n", (int)response->headers[i].name_len, response->headers[i].name,
                    (int)response->headers[i].value_len, response->headers[i].value);
        gprintf("------------ COMPLETED ------------\n");
#endif
        return;
    }
    // Close on all other status codes
#ifdef DEBUG_NETWORK
    gprintf("Status code: %i - %s\n", response->status, url);
#endif
    https_close(httpinfo);
}

void downloadfile(const char *url, struct download *buffer)
{
    HTTP_INFO httpinfo = {0};
    // Always reset the size due to the image downloader looping
    if (buffer->data)
        MEM2_free(buffer->data);
    buffer->data = NULL;
    buffer->size = 0;
    // Check if we're using HTTPS and set the path
    char *path;
    if (strncmp(url, "https://", 8) == 0)
    {
        httpinfo.use_https = 1;
        path = strchr(url + 8, '/');
    }
    else if (strncmp(url, "http://", 7) == 0)
    {
        httpinfo.use_https = 0;
        path = strchr(url + 7, '/');
    }
    else
        return;
    if (!path)
        return;
    // Get the host
    int domainlength = path - url - 7 - httpinfo.use_https;
    // A redirect chooses this length, and it used to size a stack array
    if (domainlength <= 0 || domainlength > 255)
        return;
    char host[256];
    strlcpy(host, url + 7 + httpinfo.use_https, domainlength + 1);
    // Start connecting
    if (getProxyAddress() && getProxyPort() > 0)
        httpinfo.sock = tcp_connect(getProxyAddress(), getProxyPort());
    else
        httpinfo.sock = tcp_connect(host, httpinfo.use_https ? 443 : 80);

    if (httpinfo.sock < 0)
    {
#ifdef DEBUG_NETWORK
        if (httpinfo.sock == -ETIMEDOUT)
            gprintf("\nFailed to connect (timed out)\n");
        else
            gprintf("\nFailed to connect (%i)\n", httpinfo.sock);
#endif
        return;
    }
#ifdef DEBUG_NETWORK
    gprintf("\nConnected\n");
#endif
    // Connect to a web proxy
    if (getProxyAddress() && getProxyPort() > 0)
    {
        if (!connect_proxy(&httpinfo, host, getProxyUsername(), getProxyPassword()))
        {
#ifdef DEBUG_NETWORK
            gprintf("Failed to connect to proxy (%s:%i)\n", getProxyAddress(), getProxyPort());
#endif
            https_close(&httpinfo);
            return;
        }
        session = NULL; // Resume doesn't work with a proxy
#ifdef DEBUG_NETWORK
        gprintf("Proxy is ready to receive\n");
#endif
    }
    // Setup for HTTPS if it's necessary
    if (httpinfo.use_https)
    {
        // Create a new SSL context
        // TLS 1.2 is slightly faster on Wii
        if ((httpinfo.ctx = wolfSSL_CTX_new(wolfTLSv1_2_client_method())) == NULL)
        {
#ifdef DEBUG_NETWORK
            gprintf("Failed to create WOLFSSL_CTX\n");
#endif
            https_close(&httpinfo);
            return;
        }
        // Don't verify certificates
        wolfSSL_CTX_set_verify(httpinfo.ctx, WOLFSSL_VERIFY_NONE, 0);
        // Enable SNI
        if (wolfSSL_CTX_UseSNI(httpinfo.ctx, 0, host, strlen(host)) != WOLFSSL_SUCCESS)
        {
#ifdef DEBUG_NETWORK
            gprintf("Failed to set SNI\n");
#endif
            https_close(&httpinfo);
            return;
        }
        // Custom I/O is essential due to how libogc handles errors
        wolfSSL_CTX_SetIOSend(httpinfo.ctx, send_callback);
        wolfSSL_CTX_SetIORecv(httpinfo.ctx, recv_callback);
        // Create a new wolfSSL session
        if ((httpinfo.ssl = wolfSSL_new(httpinfo.ctx)) == NULL)
        {
#ifdef DEBUG_NETWORK
            gprintf("SSL session creation failed\n");
#endif
            https_close(&httpinfo);
            return;
        }
        // Set the file descriptor
        if (wolfSSL_set_fd(httpinfo.ssl, httpinfo.sock) != WOLFSSL_SUCCESS)
        {
#ifdef DEBUG_NETWORK
            gprintf("Failed to set SSL file descriptor\n");
#endif
            https_close(&httpinfo);
            return;
        }
        // Attempt to resume the session
        if (session && wolfSSL_set_session(httpinfo.ssl, session) != WOLFSSL_SUCCESS)
        {
#ifdef DEBUG_NETWORK
            gprintf("Failed to set session (session timed out?)\n");
#endif
            session = NULL;
        }
        // Initiate a handshake
        u64 time = gettime();
        while (true)
        {
            if (ticks_to_millisecs(diff_ticks(time, gettime())) > CONNECT_TIMEOUT)
            {
#ifdef DEBUG_NETWORK
                gprintf("SSL handshake failed\n");
#endif
                https_close(&httpinfo);
                return;
            }
            if (wolfSSL_connect(httpinfo.ssl) == WOLFSSL_SUCCESS)
                break;
            usleep(10000);
        }
        // Check if we resumed successfully
        if (session && !wolfSSL_session_reused(httpinfo.ssl))
        {
#ifdef DEBUG_NETWORK
            gprintf("Failed to resume session\n");
#endif
            session = NULL;
        }
        // Cipher info
#ifdef DEBUG_NETWORK
        /*char ciphers[4096];
        wolfSSL_get_ciphers(ciphers, (int)sizeof(ciphers));
        gprintf("All supported ciphers: %s\n", ciphers);*/
        WOLFSSL_CIPHER *cipher = wolfSSL_get_current_cipher(httpinfo.ssl);
        gprintf("Using: %s - %s\n", wolfSSL_get_version(httpinfo.ssl), wolfSSL_CIPHER_get_name(cipher));
#endif
        // Save the session
        session = wolfSSL_get_session(httpinfo.ssl);
    }
    // Send our request
    char request[2300];
    int ret, len;
    len = snprintf(request, sizeof(request),
                   "%s %s HTTP/1.1\r\n"
                   "Host: %s\r\n"
                   "User-Agent: USBLoaderGX/%s\r\n"
                   "Connection: close\r\n"
                   "Pragma: no-cache\r\n"
                   "Cache-Control: no-cache\r\n\r\n",
                   buffer->gametdbcheck ? "HEAD" : "GET", path, host, LOADER_REV);
    if ((ret = https_write(&httpinfo, request, len, false)) != len)
    {
#ifdef DEBUG_NETWORK
        gprintf("https_write error: %i\n", ret);
#endif
        https_close(&httpinfo);
        return;
    }
    // Check if we want a response
    if (buffer->skip_response)
    {
#ifdef DEBUG_NETWORK
        gprintf("Sent request to %s and skipping response\n", host);
#endif
        https_close(&httpinfo);
        return;
    }
    // Get the response, on the heap: HTTP_RESPONSE holds 16 KB of headers,
    // and a redirect calls downloadfile() again from handle_response().
    HTTP_RESPONSE *response = (HTTP_RESPONSE *)MEM2_alloc(sizeof(HTTP_RESPONSE));
    if (!response)
    {
        https_close(&httpinfo);
        return;
    }
    memset(response, 0, sizeof(*response));
    if (get_response(&httpinfo, response, false))
        handle_response(&httpinfo, response, buffer, url);
    else
        https_close(&httpinfo);
    MEM2_free(response);
}
