#include <string.h>
#include <time.h>
#include <ogcsys.h>
#include <ogc/lwp_watchdog.h>
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/sha256.h>

//! wolfSSL's seed: CUSTOM_RAND_GENERATE_SEED in deps/wolfssl/user_settings.h.
//! The PowerPC can read no random source on a Wii: libogc offers none, and
//! newlib's getentropy() has no backend. The time an IOS request takes depends
//! on what the Starlet is doing, so the low bits of the time base around such
//! requests vary; each 32 bytes of output hashes 64 of those timings with the
//! time base, the date and time, the console ID and a call counter. Dolphin
//! answers IOS in a fixed time, so there only the date and time vary.
int GXGenerateSeed(unsigned char *output, unsigned int sz)
{
	static u32 calls = 0;
	u32 deviceId = 0;
	time_t wallclock = time(NULL);
	ES_GetDeviceID(&deviceId);

	while (sz > 0)
	{
		wc_Sha256 sha;
		byte digest[WC_SHA256_DIGEST_SIZE];
		u64 now = gettime();

		if (wc_InitSha256(&sha) != 0)
			return -1;
		calls++;
		wc_Sha256Update(&sha, (const byte *)&now, sizeof(now));
		wc_Sha256Update(&sha, (const byte *)&wallclock, sizeof(wallclock));
		wc_Sha256Update(&sha, (const byte *)&deviceId, sizeof(deviceId));
		wc_Sha256Update(&sha, (const byte *)&calls, sizeof(calls));

		for (int i = 0; i < 64; i++)
		{
			u32 id;
			u64 before = gettime();
			ES_GetDeviceID(&id);
			u32 took = (u32)(gettime() - before);
			wc_Sha256Update(&sha, (const byte *)&took, sizeof(took));
		}

		wc_Sha256Final(&sha, digest);
		wc_Sha256Free(&sha);

		u32 n = sz < sizeof(digest) ? sz : sizeof(digest);
		memcpy(output, digest, n);
		output += n;
		sz -= n;
	}
	return 0;
}
