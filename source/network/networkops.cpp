/****************************************************************************
 * Thread-Safe Network Operations
 * for USB Loader GX
 *
 * HTTP operations
 * Based on dhewg/bushing, modified by dimok, made thread-safe by blackb0x
 ****************************************************************************/

#include <hbc_agent.h>
#include <ogcsys.h>
#include <string.h>
#include <stdio.h>
#include <ogc/lwp.h>
#include <ogc/mutex.h>
#include <ogc/lwp_watchdog.h>
#include <errno.h>

#include "networkops.h"
#include "https.h"
#include "update.h"
#include "gecko.h"
#include "settings/ProxySettings.h"

#define PORT 4299
//! How long a wiiload sender may go quiet before the read gives up
#define WIILOAD_STALL_MS 10000

/*** Incoming filesize ***/
u32 infilesize = 0;
u32 uncfilesize = 0;

s32 connection = -1;
static s32 listenSocket = -1;
static bool networkinitialized = false;
static bool checkincomming = false;
static bool waitforanswer = false;
static char IP[16] = {0};
static char incommingIP[50] = {0};
char wiiloadVersion[2] = {0};

static lwp_t networkthread = LWP_THREAD_NULL;
static bool networkHalt = true;

// Mutex for all shared variables
static mutex_t net_mutex = LWP_MUTEX_NULL;

// Helper macros for locking
#define NET_LOCK() LWP_MutexLock(net_mutex)
#define NET_UNLOCK() LWP_MutexUnlock(net_mutex)

/****************************************************************************
 * Initialize_Network
 ***************************************************************************/
void Initialize_Network(int retries)
{
	NET_LOCK();
	if (networkinitialized)
	{
		NET_UNLOCK();
		return;
	}
	NET_UNLOCK();

	// The hbc agent starts the network in its own thread, and libogc's
	// net_init() hangs if it runs while another thread's start-up is in
	// progress. This returns at once when none is.
	hbc_agent_net_wait(30000);

	s32 result = if_config(IP, NULL, NULL, true, retries);

	NET_LOCK();
	if (result < 0)
	{
		networkinitialized = false;
	}
	else
	{
		getProxyInfo();
		wolfSSL_Init();
		networkinitialized = true;
		gprintf("Initialized network\n");
	}
	NET_UNLOCK();
}

/****************************************************************************
 * DeinitNetwork
 ***************************************************************************/
void DeinitNetwork(void)
{
	NET_LOCK();
	wolfSSL_Cleanup();
	net_wc24cleanup();
	net_deinit();
	networkinitialized = false;
	NET_UNLOCK();
}

/****************************************************************************
 * Check if network was initialised
 ***************************************************************************/
bool IsNetworkInit(void)
{
	NET_LOCK();
	bool ret = networkinitialized;
	NET_UNLOCK();
	return ret;
}

/****************************************************************************
 * Get network IP
 ***************************************************************************/
char *GetNetworkIP(void)
{
	NET_LOCK();
	static char ip_copy[16];
	strlcpy(ip_copy, IP, sizeof(ip_copy));
	NET_UNLOCK();
	return ip_copy;
}

/****************************************************************************
 * Get incomming IP
 ***************************************************************************/
char *GetIncommingIP(void)
{
	NET_LOCK();
	static char ip_copy[50];
	strlcpy(ip_copy, incommingIP, sizeof(ip_copy));
	NET_UNLOCK();
	return ip_copy;
}

/****************************************************************************
 * Read network data
 *
 * The wiiload connection is non-blocking, and a read gives up when the sender
 * sends nothing for WIILOAD_STALL_MS. Anything on the LAN can open it and then
 * say nothing: a blocking read held the network thread, HaltNetworkThread()
 * waits for that thread on the main thread, and leaving the homebrew browser
 * froze the loader. Returns what arrived, or a negative error if nothing did.
 ***************************************************************************/
static s32 read_with_stall_timeout(s32 sock, u8 *buf, u32 len)
{
	u32 read = 0;
	u64 lastData = gettime();

	while (read < len)
	{
		s32 ret = net_read(sock, buf + read, len - read);
		if (ret > 0)
		{
			read += ret;
			lastData = gettime();
			continue;
		}
		if (ret == 0)
			break;
		if (ret != -EAGAIN)
			return read > 0 ? (s32) read : ret;
		if (ticks_to_millisecs(diff_ticks(lastData, gettime())) > WIILOAD_STALL_MS)
			return read > 0 ? (s32) read : -ETIMEDOUT;
		usleep(10000);
	}
	return read;
}

s32 network_read(s32 connect, u8 *buf, u32 len)
{
	NET_LOCK();
	s32 conn = (connect == NET_DEFAULT_SOCK) ? connection : connect;
	NET_UNLOCK();

	return read_with_stall_timeout(conn, buf, len);
}

/****************************************************************************
 * Close the connection
 ***************************************************************************/
void CloseConnection()
{
	NET_LOCK();
	if (connection >= 0)
	{
		net_close(connection);
		connection = -1;
	}
	if (waitforanswer && listenSocket >= 0)
	{
		net_close(listenSocket);
		listenSocket = -1;
		waitforanswer = false;
	}
	NET_UNLOCK();
}

/****************************************************************************
 * The wiiload server's non-blocking listening socket
 ***************************************************************************/
static s32 OpenListenSocket()
{
	s32 sock = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (sock == INVALID_SOCKET)
		return -1;

	struct sockaddr_in sin;
	sin.sin_family = AF_INET;
	sin.sin_port = htons(PORT);
	sin.sin_addr.s_addr = htonl(INADDR_ANY);

	int flags = net_fcntl(sock, F_GETFL, 0);
	net_fcntl(sock, F_SETFL, flags | 4);

	if (net_bind(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0 || net_listen(sock, 3) < 0)
	{
		net_close(sock);
		return -1;
	}
	return sock;
}

/****************************************************************************
 * NetworkWait
 ***************************************************************************/
int NetworkWait()
{
	NET_LOCK();
	if (!checkincomming)
	{
		NET_UNLOCK();
		return -3;
	}
	NET_UNLOCK();

	struct sockaddr_in client_address;
	socklen_t addrlen = sizeof(client_address);

	//! The listening socket stays open while GX waits. It was opened and closed
	//! again around each non-blocking accept, 100 ms apart, so a sender got in
	//! only if its connect landed in the few milliseconds the socket existed.
	NET_LOCK();
	s32 local_socket = listenSocket;
	NET_UNLOCK();
	if (local_socket < 0)
	{
		local_socket = OpenListenSocket();
		if (local_socket < 0)
			return -1;
		NET_LOCK();
		listenSocket = local_socket;
		NET_UNLOCK();
	}

	s32 local_connection = net_accept(local_socket, (struct sockaddr *)&client_address, &addrlen);

	if (local_connection < 0)
	{
		//! Nobody there yet. Any other error (a network re-init closed the
		//! socket) needs a new one.
		if (local_connection != -EAGAIN)
		{
			NET_LOCK();
			if (listenSocket == local_socket)
			{
				net_close(local_socket);
				listenSocket = -1;
			}
			NET_UNLOCK();
		}
		return -4;
	}

	NET_LOCK();
	connection = local_connection;
	snprintf(incommingIP, sizeof(incommingIP), "%s", inet_ntoa(client_address.sin_addr));
	NET_UNLOCK();

	{
		//! Not every IOS passes O_NONBLOCK on to an accepted socket
		int connFlags = net_fcntl(local_connection, F_GETFL, 0);
		net_fcntl(local_connection, F_SETFL, (connFlags < 0 ? 0 : connFlags) | 4);

		//! Anything on the network can open this connection, so a short read
		//! must not leave the version or either size as whatever was there.
		unsigned char haxx[9] = {0};
		bool gotHeader = (read_with_stall_timeout(local_connection, haxx, 8) == 8);

		if (gotHeader)
		{
			NET_LOCK();
			wiiloadVersion[0] = haxx[4];
			wiiloadVersion[1] = haxx[5];
			NET_UNLOCK();

			infilesize = 0;
			uncfilesize = 0;

			gotHeader = (read_with_stall_timeout(local_connection, (u8 *) &infilesize, 4) == 4);

			if (gotHeader && (haxx[4] > 0 || haxx[5] > 4))
				gotHeader = (read_with_stall_timeout(local_connection, (u8 *) &uncfilesize, 4) == 4);
		}

		if (!gotHeader)
		{
			gprintf("wiiload: no header from %s\n", inet_ntoa(client_address.sin_addr));
			net_close(local_connection);
			NET_LOCK();
			connection = -1;
			NET_UNLOCK();
			return -4;
		}

		NET_LOCK();
		waitforanswer = true;
		checkincomming = false;
		networkHalt = true;
		NET_UNLOCK();
	}

	return 1;
}

/****************************************************************************
 * HaltNetwork
 ***************************************************************************/
void HaltNetworkThread()
{
	NET_LOCK();
	networkHalt = true;
	checkincomming = false;
	bool need_close = waitforanswer;
	NET_UNLOCK();

	// wait for thread to finish: a wiiload read in it gives up after WIILOAD_STALL_MS,
	// and the sockets it uses are closed only once it has stopped
	gprintf("HaltNetworkThread\n");
	while (!LWP_ThreadIsSuspended(networkthread))
		usleep(100);

	if (need_close)
		CloseConnection();

	// GX's wiiload server is done with TCP 4299; the agent listens again.
	hbc_agent_listen(true);

//	LWP_SuspendThread(networkthread);
}

/****************************************************************************
 * ResumeNetworkThread
 ***************************************************************************/
void ResumeNetworkThread()
{
	NET_LOCK();
	networkHalt = false;
	NET_UNLOCK();
	if (LWP_ThreadIsSuspended(networkthread))
		LWP_ResumeThread(networkthread);
}

/****************************************************************************
 * Resume NetworkWait
 ***************************************************************************/
void ResumeNetworkWait()
{
	// The hbc agent listens on the same port (TCP 4299) as GX's wiiload server
	// in NetworkWait(); it lets go until HaltNetworkThread().
	hbc_agent_listen(false);

	NET_LOCK();
	networkHalt = true;
	checkincomming = true;
	waitforanswer = true;
	infilesize = 0;
	connection = -1;
	NET_UNLOCK();
	LWP_ResumeThread(networkthread);
}

/*********************************************************************************
 * Networkthread for background network initialize
 *********************************************************************************/
static void *networkinitcallback(void *arg)
{
	while (1)
	{
		NET_LOCK();
		bool halt = networkHalt;
		bool check = checkincomming;
		NET_UNLOCK();

		if (!check && halt)
			LWP_SuspendThread(networkthread);

		Initialize_Network(5);

		NET_LOCK();
		bool initialized = networkinitialized;
		NET_UNLOCK();

		if (initialized)
		{
			NET_LOCK();
			networkHalt = true;
			NET_UNLOCK();
		}

		NET_LOCK();
		check = checkincomming;
		NET_UNLOCK();

		if (check)
			NetworkWait();

		usleep(100000);
	}
	return NULL;
}

/****************************************************************************
 * InitNetworkThread with priority 0 (idle)
 ***************************************************************************/
void InitNetworkThread()
{
	if (net_mutex == LWP_MUTEX_NULL)
		LWP_MutexInit(&net_mutex, false);
	LWP_CreateThread(&networkthread, networkinitcallback, NULL, NULL, 16384, 0);
}

/****************************************************************************
 * ShutdownThread
 ***************************************************************************/
void ShutdownNetworkThread()
{
	LWP_JoinThread(networkthread, NULL);
	networkthread = LWP_THREAD_NULL;
	if (net_mutex != LWP_MUTEX_NULL)
	{
		LWP_MutexDestroy(net_mutex);
		net_mutex = LWP_MUTEX_NULL;
	}
}