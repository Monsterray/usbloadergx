
#ifndef _GECKO_H_
#define _GECKO_H_

#ifdef __cplusplus
extern "C"
{
#endif

	char ascii(char s);

#ifndef NO_DEBUG
	//use this just like printf();
	void gprintf(const char *str, ...) __attribute__((format(printf, 1, 2)));
	//! Copy every gprintf() line to this file too (truncated first); NULL stops it
	void gprintf_tee(const char *path);
	bool InitGecko();
	void hexdump(void *d, int len);
	void USBGeckoOutput();
#else
#define gprintf(...)
#define gprintf_tee(x)
#define InitGecko()	  false
#define hexdump( x, y )
#endif /* NO_DEBUG */

#ifdef __cplusplus
}
#endif

#endif

