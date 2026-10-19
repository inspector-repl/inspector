#ifndef REPLXX_IO_HXX_INCLUDED
#define REPLXX_IO_HXX_INCLUDED 1

#include <deque>

#ifdef _WIN32
#include <vector>
#include <windows.h>
#else
#include <termios.h>
#include <signal.h>
#endif

#include "utf8string.hxx"

namespace replxx {

class Terminal {
public:
	enum class EVENT_TYPE {
		KEY_PRESS,
		MESSAGE,
		TIMEOUT,
		RESIZE
	};
private:
#ifdef _WIN32
	HANDLE _consoleOut;
	HANDLE _consoleIn;
	DWORD _origOutMode;
	DWORD _origInMode;
	bool _autoEscape;
	WORD _oldDisplayAttribute;
	UINT const _inputCodePage;
	UINT const _outputCodePage;
	HANDLE _interrupt;
	typedef std::deque<EVENT_TYPE> events_t;
	events_t _events;
	std::vector<char> _empty;
#else
	struct termios _origTermios; /* in order to restore at exit */
	struct termios _rawModeTermios; /* in order to reset raw mode after callbacks */
	int _interrupt[2];
	char8_t _utf8String[5] = {}; /* partially read UTF-8 sequence */
	size_t _utf8Count = 0;
	bool _windowChangeHandlerInstalled = false; /* whether this terminal receives `SIGWINCH` */
	struct sigaction _origWindowChangeHandler;
#endif
	int _escDelay;
	bool _rawMode; /* for destructor to check if restore is needed */
	Utf8String _utf8;

	int _in_fd = 0;
	int _out_fd = 1;
	int _err_fd = 2;

public:
	enum class CLEAR_SCREEN {
		WHOLE,
		TO_END
	};
public:
	explicit Terminal(int in_fd_= 0, int out_fd_ = 1);
	~Terminal( void );
	void write32( char32_t const*, int );
	void write8( char const*, int );
	int get_screen_columns(void);
	int get_screen_rows(void);
	void enable_bracketed_paste( void );
	void disable_bracketed_paste( void );
	int enable_raw_mode(void);
	int reset_raw_mode(void);
	void disable_raw_mode(void);
	char32_t read_char(void);
	void clear_screen( CLEAR_SCREEN );
	EVENT_TYPE wait_for_input( int long = 0 );
	void notify_event( EVENT_TYPE );
	void jump_cursor( int, int );
	void set_cursor_visible( bool );
	void set_escdelay(int delay);
#ifndef _WIN32
	/* A positive delay limits the wait, zero waits indefinitely, a negative one does not wait. */
	char32_t read_unicode_character( int escDelay_ = 0 );
	int read_verbatim( char32_t*, int );
	int install_window_change_handler( void );
#endif
private:
	void enable_out( void );
	void disable_out( void );
private:
	Terminal( Terminal const& ) = delete;
	Terminal& operator = ( Terminal const& ) = delete;
	Terminal( Terminal&& ) = delete;
	Terminal& operator = ( Terminal&& ) = delete;
};

void beep(int fd_);

namespace tty {

extern bool is_a_tty( int fd_ );

/**
extern bool in;
extern bool out;
*/

}

}

#endif

