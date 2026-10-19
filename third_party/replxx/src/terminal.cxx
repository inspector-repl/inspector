#include <atomic>
#include <memory>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <array>
#include <stdexcept>

#ifdef _WIN32

#include <conio.h>
#include <windows.h>
#include <io.h>
#define isatty _isatty
#define strcasecmp _stricmp
#define strdup _strdup
#define write _write
#define STDIN_FILENO 0

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
static DWORD const ENABLE_VIRTUAL_TERMINAL_PROCESSING = 4;
#endif

#include "windows.hxx"

#else /* _WIN32 */

#include <unistd.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <fcntl.h>
#include <signal.h>
#include <limits.h>

#endif /* _WIN32 */

#include "terminal.hxx"
#include "conversion.hxx"
#include "escape.hxx"
#include "replxx.hxx"
#include "util.hxx"

using namespace std;

namespace replxx {

namespace tty {

bool is_a_tty( int fd_ ) {
	bool aTTY( isatty( fd_ ) != 0 );
#ifdef _WIN32
	do {
		if ( aTTY ) {
			break;
		}
		HANDLE h( (HANDLE)_get_osfhandle( fd_ ) );
		if ( h == INVALID_HANDLE_VALUE ) {
			break;
		}
		DWORD st( 0 );
		if ( ! GetConsoleMode( h, &st ) ) {
			break;
		}
		aTTY = true;
	} while ( false );
#endif
	return ( aTTY );
}

/**
The patch to support arbitrary file descriptors made these functions unusable.
bool in( is_a_tty( 0 ) );
bool out( is_a_tty( 1 ) );
*/

}

#ifndef _WIN32
/* The terminal that receives the `SIGWINCH` notifications. Signals are process-wide, so only
 * a terminal that installed the window change handler registers itself here: several terminals
 * may live in one process (e.g. client sessions embedded into a server, attached to their own
 * pseudo-terminals) and they must not overwrite each other's registration. */
static std::atomic<Terminal*> _terminal_{ nullptr };
static void WindowSizeChanged( int ) {
	Terminal* terminal( _terminal_.load() );
	if ( ! terminal ) {
		return;
	}
	terminal->notify_event( Terminal::EVENT_TYPE::RESIZE );
}
#endif


Terminal::Terminal(int in_fd_, int out_fd_)
#ifdef _WIN32
	: _consoleOut( INVALID_HANDLE_VALUE )
	, _consoleIn( INVALID_HANDLE_VALUE )
	, _origOutMode()
	, _origInMode()
	, _oldDisplayAttribute()
	, _inputCodePage( GetConsoleCP() )
	, _outputCodePage( GetConsoleOutputCP() )
	, _interrupt( INVALID_HANDLE_VALUE )
	, _events()
	, _empty()
#else
	: _origTermios()
	, _rawModeTermios()
	, _interrupt()
#endif
	, _escDelay(0)
	, _rawMode( false )
	, _utf8()
	, _in_fd(in_fd_)
	, _out_fd(out_fd_) {
#ifdef _WIN32
	_interrupt = CreateEvent( nullptr, true, false, TEXT( "replxx_interrupt_event" ) );
#else
	static_cast<void>( ::pipe( _interrupt ) == 0 );
#endif
}

Terminal::~Terminal( void ) {
#ifndef _WIN32
	if ( _windowChangeHandlerInstalled ) {
		sigaction( SIGWINCH, &_origWindowChangeHandler, nullptr );
	}
#endif
	if ( _rawMode ) {
		disable_raw_mode();
	}
#ifdef _WIN32
	CloseHandle( _interrupt );
#else
	static_cast<void>( ::close( _interrupt[0] ) == 0 );
	static_cast<void>( ::close( _interrupt[1] ) == 0 );
#endif
}

void Terminal::write32( char32_t const* text32, int len32 ) {
	_utf8.assign( text32, len32 );
	write8( _utf8.get(), _utf8.size() );
	return;
}

void Terminal::write8( char const* data_, int size_ ) {
#ifdef _WIN32
	bool temporarilyEnabled( false );
	if ( _consoleOut == INVALID_HANDLE_VALUE ) {
		enable_out();
		temporarilyEnabled = true;
	}
	int nWritten( win_write( _consoleOut, _autoEscape, data_, size_ ) );
	if ( temporarilyEnabled ) {
		disable_out();
	}
#else
	int nWritten( write( _out_fd, data_, size_ ) );
#endif
	if ( nWritten != size_ ) {
		throw std::runtime_error( "write failed" );
	}
	return;
}

int Terminal::get_screen_columns( void ) {
	int cols( 0 );
#ifdef _WIN32
	CONSOLE_SCREEN_BUFFER_INFO inf;
	GetConsoleScreenBufferInfo( _consoleOut, &inf );
	cols = inf.dwSize.X;
#else
	struct winsize ws;
	cols = ( ioctl( _out_fd, TIOCGWINSZ, &ws ) == -1 ) ? 80 : ws.ws_col;
#endif
	// cols is 0 in certain circumstances like inside debugger, which creates
	// further issues
	return ( cols > 0 ) ? cols : 80;
}

int Terminal::get_screen_rows( void ) {
	int rows;
#ifdef _WIN32
	CONSOLE_SCREEN_BUFFER_INFO inf;
	GetConsoleScreenBufferInfo( _consoleOut, &inf );
	rows = 1 + inf.srWindow.Bottom - inf.srWindow.Top;
#else
	struct winsize ws;
	rows = (ioctl(_out_fd, TIOCGWINSZ, &ws) == -1) ? 24 : ws.ws_row;
#endif
	return (rows > 0) ? rows : 24;
}

namespace {
inline int notty( void ) {
	errno = ENOTTY;
	return ( -1 );
}
}

void Terminal::enable_out( void ) {
#ifdef _WIN32
	SetConsoleOutputCP( 65001 );
	_consoleOut = GetStdHandle( STD_OUTPUT_HANDLE );
	GetConsoleMode( _consoleOut, &_origOutMode );
	_autoEscape = SetConsoleMode( _consoleOut, _origOutMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING ) != 0;
#endif
}

void Terminal::disable_out( void ) {
#ifdef _WIN32
	SetConsoleMode( _consoleOut, _origOutMode );
	SetConsoleOutputCP( _outputCodePage );
	_consoleOut = INVALID_HANDLE_VALUE;
	_autoEscape = false;
#endif
}

void Terminal::enable_bracketed_paste( void ) {
	static char const  BRACK_PASTE_INIT[] = "\033[?2004h";
	write8( BRACK_PASTE_INIT, sizeof ( BRACK_PASTE_INIT ) - 1 );
}

void Terminal::disable_bracketed_paste( void ) {
	static char const  BRACK_PASTE_DISABLE[] = "\033[?2004l";
	write8( BRACK_PASTE_DISABLE, sizeof ( BRACK_PASTE_DISABLE ) - 1 );
}

int Terminal::enable_raw_mode( void ) {
	if ( _rawMode ) {
		return ( 0 );
	}
#ifdef _WIN32
	_consoleIn = GetStdHandle( STD_INPUT_HANDLE );
	GetConsoleMode( _consoleIn, &_origInMode );
#else

	if ( ! tty::is_a_tty(_in_fd) ) {
		return ( notty() );
	}
	if ( tcgetattr( _in_fd, &_origTermios ) == -1 ) {
		return ( notty() );
	}

	_rawModeTermios = _origTermios; /* modify the original mode */
	/* input modes: no break, no CR to NL, no parity check, no strip char,
	 * no start/stop output control. */
	_rawModeTermios.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	/* output modes - disable post processing */
	// this is wrong, we don't want _rawModeTermios output, it turns newlines into straight
	// linefeeds
	// _rawModeTermios.c_oflag &= ~(OPOST);
	/* control modes - set 8 bit chars */
	_rawModeTermios.c_cflag |= (CS8);
	/* local modes - echoing off, canonical off, no extended functions,
	 * no signal chars (^Z,^C) */
	_rawModeTermios.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	/* control chars - set return condition: min number of bytes and timer.
	 * We want read to return every single byte, without timeout. */
	_rawModeTermios.c_cc[VMIN] = 1;
	_rawModeTermios.c_cc[VTIME] = 0; /* 1 byte, no timer */

#endif

	_rawMode = true;
	if ( reset_raw_mode() < 0 ) {
		_rawMode = false;
		return ( notty() );
	}

#ifndef _WIN32
	if ( _windowChangeHandlerInstalled ) {
		_terminal_.store( this );
	}
#endif
	return ( 0 );
}

int Terminal::reset_raw_mode( void ) {
	if ( ! _rawMode ) {
		return ( -1 );
	}
#ifdef _WIN32
	SetConsoleMode(
		_consoleIn,
		( _origInMode & ~( ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT ) ) | ENABLE_QUICK_EDIT_MODE
	);
	SetConsoleCP( 65001 );
	enable_out();
	return ( 0 );
#else
	/* put terminal in raw mode after flushing */
	return ( tcsetattr( _in_fd, TCSADRAIN, &_rawModeTermios ) );
#endif
}

void Terminal::set_escdelay(int delay) {
	_escDelay = delay;
}

void Terminal::disable_raw_mode(void) {
	if ( ! _rawMode ) {
		return;
	}
#ifdef _WIN32
	disable_out();
	SetConsoleMode( _consoleIn, _origInMode );
	SetConsoleCP( _inputCodePage );
	_consoleIn = INVALID_HANDLE_VALUE;
#else
	Terminal* expected( this );
	_terminal_.compare_exchange_strong( expected, nullptr );
	if ( tcsetattr( _in_fd, TCSADRAIN, &_origTermios ) == -1 ) {
		return;
	}
#endif
	_rawMode = false;
	return;
}

#ifndef _WIN32

/**
 * Read a UTF-8 sequence from the non-Windows keyboard and return the Unicode
 * (char32_t) character it encodes
 *
 * @return char32_t Unicode character
 */
char32_t Terminal::read_unicode_character( int escDelay_ ) {
	while (true) {
		char8_t c;

		/* Continue reading if interrupted by signal. */
		ssize_t nread;
		struct pollfd fd = { .fd = _in_fd, .events = POLLIN };
		do {
			errno = 0;
			/* A positive delay limits the wait, zero waits indefinitely, a negative one does not wait. */
			nread = poll(&fd, 1, escDelay_ > 0 ? escDelay_ : ( escDelay_ < 0 ? 0 : -1 ));
		} while ((nread < 0) && (errno == EINTR || errno == EAGAIN));

		/* Read on any event, not only `POLLIN`: on a hangup or an error without pending input, `read`
		 * reports the end of input or the error, and `c` is not used. */
		if (nread > 0) nread = read( _in_fd, &c, 1 );

		if (nread <= 0) return 0;
		if (c <= 0x7F || locale::is8BitEncoding) { // short circuit ASCII
			_utf8Count = 0;
			return c;
		} else if (_utf8Count < sizeof(_utf8String) - 1) {
			_utf8String[_utf8Count++] = c;
			_utf8String[_utf8Count] = 0;
			char32_t unicodeChar[2];
			int ucharCount( 0 );
			ConversionResult res = copyString8to32(unicodeChar, 2, ucharCount, _utf8String);
			if (res == conversionOK && ucharCount) {
				_utf8Count = 0;
				return unicodeChar[0];
			}
		} else {
			_utf8Count = 0; // this shouldn't happen: got four bytes but no UTF-8 character
		}
	}
}

#endif // #ifndef _WIN32

void beep(int fd_) {
	dprintf(fd_, "\x7");	// ctrl-G == bell/beep
	fsync(fd_);
}

// replxx_read_char -- read a keystroke or keychord from the keyboard, and translate it
// into an encoded "keystroke".	When convenient, extended keys are translated into their
// simpler Emacs keystrokes, so an unmodified "left arrow" becomes Ctrl-B.
//
// A return value of zero means "no input available", and a return value of -1
// means "invalid key".
//
char32_t Terminal::read_char( void ) {
	char32_t c( 0 );
#ifdef _WIN32
	INPUT_RECORD rec;
	DWORD count;
	char32_t modifierKeys = 0;
	bool escSeen = false;
	int highSurrogate( 0 );
	while (true) {
		ReadConsoleInputW( _consoleIn, &rec, 1, &count );
#if __REPLXX_DEBUG__	// helper for debugging keystrokes, display info in the debug "Output"
		// window in the debugger
		{
			if ( rec.EventType == KEY_EVENT ) {
				//if ( rec.Event.KeyEvent.uChar.UnicodeChar ) {
				char buf[1024];
				sprintf(
					buf,
					"Unicode character 0x%04X, repeat count %d, virtual keycode 0x%04X, "
					"virtual scancode 0x%04X, key %s%s%s%s%s\n",
					rec.Event.KeyEvent.uChar.UnicodeChar,
					rec.Event.KeyEvent.wRepeatCount,
					rec.Event.KeyEvent.wVirtualKeyCode,
					rec.Event.KeyEvent.wVirtualScanCode,
					rec.Event.KeyEvent.bKeyDown ? "down" : "up",
					(rec.Event.KeyEvent.dwControlKeyState & LEFT_CTRL_PRESSED) ? " L-Ctrl" : "",
					(rec.Event.KeyEvent.dwControlKeyState & RIGHT_CTRL_PRESSED) ? " R-Ctrl" : "",
					(rec.Event.KeyEvent.dwControlKeyState & LEFT_ALT_PRESSED) ? " L-Alt" : "",
					(rec.Event.KeyEvent.dwControlKeyState & RIGHT_ALT_PRESSED) ? " R-Alt" : ""
				);
				OutputDebugStringA( buf );
				//}
			}
		}
#endif
		if ( rec.EventType != KEY_EVENT ) {
			continue;
		}
		// Windows provides for entry of characters that are not on your keyboard by sending the
		// Unicode characters as a "key up" with virtual keycode 0x12 (VK_MENU == Alt key) ...
		// accept these characters, otherwise only process characters on "key down"
		if ( !rec.Event.KeyEvent.bKeyDown && ( rec.Event.KeyEvent.wVirtualKeyCode != VK_MENU ) ) {
			continue;
		}
		modifierKeys = 0;
		// AltGr is encoded as ( LEFT_CTRL_PRESSED | RIGHT_ALT_PRESSED ), so don't treat this
		// combination as either CTRL or META we just turn off those two bits, so it is still
		// possible to combine CTRL and/or META with an AltGr key by using right-Ctrl and/or
		// left-Alt
		DWORD const AltGr( LEFT_CTRL_PRESSED | RIGHT_ALT_PRESSED );
		if ( ( rec.Event.KeyEvent.dwControlKeyState & AltGr ) == AltGr ) {
			rec.Event.KeyEvent.dwControlKeyState &= ~( LEFT_CTRL_PRESSED | RIGHT_ALT_PRESSED );
		}
		if ( rec.Event.KeyEvent.dwControlKeyState & ( RIGHT_CTRL_PRESSED | LEFT_CTRL_PRESSED ) ) {
			modifierKeys |= Replxx::KEY::BASE_CONTROL;
		}
		if ( rec.Event.KeyEvent.dwControlKeyState & ( RIGHT_ALT_PRESSED | LEFT_ALT_PRESSED ) ) {
			modifierKeys |= Replxx::KEY::BASE_META;
		}
		if ( escSeen ) {
			modifierKeys |= Replxx::KEY::BASE_META;
		}
		int key( rec.Event.KeyEvent.uChar.UnicodeChar );
		if ( key == 0 ) {
			if ( rec.Event.KeyEvent.dwControlKeyState & SHIFT_PRESSED ) {
				modifierKeys |= Replxx::KEY::BASE_SHIFT;
			}
			switch ( rec.Event.KeyEvent.wVirtualKeyCode ) {
				case VK_LEFT:
					return modifierKeys | Replxx::KEY::LEFT;
				case VK_RIGHT:
					return modifierKeys | Replxx::KEY::RIGHT;
				case VK_UP:
					return modifierKeys | Replxx::KEY::UP;
				case VK_DOWN:
					return modifierKeys | Replxx::KEY::DOWN;
				case VK_DELETE:
					return modifierKeys | Replxx::KEY::DELETE;
				case VK_HOME:
					return modifierKeys | Replxx::KEY::HOME;
				case VK_END:
					return modifierKeys | Replxx::KEY::END;
				case VK_PRIOR:
					return modifierKeys | Replxx::KEY::PAGE_UP;
				case VK_NEXT:
					return modifierKeys | Replxx::KEY::PAGE_DOWN;
				case VK_F1:
					return modifierKeys | Replxx::KEY::F1;
				case VK_F2:
					return modifierKeys | Replxx::KEY::F2;
				case VK_F3:
					return modifierKeys | Replxx::KEY::F3;
				case VK_F4:
					return modifierKeys | Replxx::KEY::F4;
				case VK_F5:
					return modifierKeys | Replxx::KEY::F5;
				case VK_F6:
					return modifierKeys | Replxx::KEY::F6;
				case VK_F7:
					return modifierKeys | Replxx::KEY::F7;
				case VK_F8:
					return modifierKeys | Replxx::KEY::F8;
				case VK_F9:
					return modifierKeys | Replxx::KEY::F9;
				case VK_F10:
					return modifierKeys | Replxx::KEY::F10;
				case VK_F11:
					return modifierKeys | Replxx::KEY::F11;
				case VK_F12:
					return modifierKeys | Replxx::KEY::F12;
				default:
					continue; // in raw mode, ReadConsoleInput shows shift, ctrl - ignore them
			}
		} else if ( key == Replxx::KEY::ESCAPE ) { // ESC, set flag for later
			if ( _escDelay > 0 ) {
				c = key;
				break;
			}
			else {
				escSeen = true;
				continue;
			}
		} else if ( ( key >= 0xD800 ) && ( key <= 0xDBFF ) ) {
			highSurrogate = key - 0xD800;
			continue;
		} else {
			if ( ( key == 13 ) && ( rec.Event.KeyEvent.dwControlKeyState & SHIFT_PRESSED ) ) {
				key = 10;
			}
			// we got a real character, return it
			if ( ( key >= 0xDC00 ) && ( key <= 0xDFFF ) ) {
				key -= 0xDC00;
				key |= ( highSurrogate << 10 );
				key += 0x10000;
			}
			if ( is_control_code( key ) ) {
				key = control_to_human( key );
				modifierKeys |= Replxx::KEY::BASE_CONTROL;
			}
			key |= modifierKeys;
			highSurrogate = 0;
			c = key;
			break;
		}
	}

#else
	c = read_unicode_character();
	if (c == 0) {
		return 0;
	}

// If _DEBUG_LINUX_KEYBOARD is set, then ctrl-^ puts us into a keyboard
// debugging mode
// where we print out decimal and decoded values for whatever the "terminal"
// program
// gives us on different keystrokes.	Hit ctrl-C to exit this mode.
//
#ifdef __REPLXX_DEBUG__
	if (c == ctrlChar('^')) {	// ctrl-^, special debug mode, prints all keys hit,
														 // ctrl-C to get out
		dprintf(_out_fd,
				"\nEntering keyboard debugging mode (on ctrl-^), press ctrl-C to exit "
				"this mode\n");
		while (true) {
			unsigned char keys[10];
			int ret = read(0, keys, 10);

			if (ret <= 0) {
				dprintf(_out_fd,"\nret: %d\n", ret);
			}
			for (int i = 0; i < ret; ++i) {
				char32_t key = static_cast<char32_t>(keys[i]);
				char* friendlyTextPtr;
				char friendlyTextBuf[10];
				const char* prefixText = (key < 0x80) ? "" : "0x80+";
				char32_t keyCopy = (key < 0x80) ? key : key - 0x80;
				if (keyCopy >= '!' && keyCopy <= '~') {	// printable
					friendlyTextBuf[0] = '\'';
					friendlyTextBuf[1] = keyCopy;
					friendlyTextBuf[2] = '\'';
					friendlyTextBuf[3] = 0;
					friendlyTextPtr = friendlyTextBuf;
				} else if (keyCopy == ' ') {
					friendlyTextPtr = const_cast<char*>("space");
				} else if (keyCopy == 27) {
					friendlyTextPtr = const_cast<char*>("ESC");
				} else if (keyCopy == 0) {
					friendlyTextPtr = const_cast<char*>("NUL");
				} else if (keyCopy == 127) {
					friendlyTextPtr = const_cast<char*>("DEL");
				} else {
					friendlyTextBuf[0] = '^';
					friendlyTextBuf[1] = control_to_human( keyCopy );
					friendlyTextBuf[2] = 0;
					friendlyTextPtr = friendlyTextBuf;
				}
				dprintf(_out_fd,"%d x%02X (%s%s)	", key, key, prefixText, friendlyTextPtr);
			}
			dprintf(_out_fd,"\x1b[1G\n");	// go to first column of new line

			// drop out of this loop on ctrl-C
			if (keys[0] == ctrlChar('C')) {
				dprintf(_out_fd,"Leaving keyboard debugging mode (on ctrl-C)\n");
				fflush(stdout);
				return -2;
			}
		}
	}
#endif // __REPLXX_DEBUG__

	c = EscapeSequenceProcessing::doDispatch(*this, _err_fd, c, _escDelay);
	if ( !(_escDelay && c == 27) && is_control_code( c ) ) {
		c = Replxx::KEY::control( control_to_human( c ) );
	}

#endif // #_WIN32
	return ( c );
}

Terminal::EVENT_TYPE Terminal::wait_for_input( int long timeout_ ) {
#ifdef _WIN32
	std::array<HANDLE, 2> handles = { _consoleIn, _interrupt };
	while ( true ) {
		DWORD event( WaitForMultipleObjects( static_cast<DWORD>( handles.size() ), handles.data(), false, timeout_ > 0 ? timeout_ : INFINITE ) );
		switch ( event ) {
			case ( WAIT_OBJECT_0 + 0 ): {
				// peek events that will be skipped
				INPUT_RECORD rec;
				DWORD count;
				PeekConsoleInputW( _consoleIn, &rec, 1, &count );

				if (
					( rec.EventType != KEY_EVENT )
					|| ( !rec.Event.KeyEvent.bKeyDown && ( rec.Event.KeyEvent.wVirtualKeyCode != VK_MENU ) )
				) {
					// read the event to unsignal the handle
					ReadConsoleInputW( _consoleIn, &rec, 1, &count );
					continue;
				} else if ( rec.EventType == KEY_EVENT ) {
					int key( rec.Event.KeyEvent.uChar.UnicodeChar );
					if ( key == 0 ) {
						switch ( rec.Event.KeyEvent.wVirtualKeyCode ) {
							case VK_LEFT:
							case VK_RIGHT:
							case VK_UP:
							case VK_DOWN:
							case VK_DELETE:
							case VK_HOME:
							case VK_END:
							case VK_PRIOR:
							case VK_NEXT:
							case VK_F1:
							case VK_F2:
							case VK_F3:
							case VK_F4:
							case VK_F5:
							case VK_F6:
							case VK_F7:
							case VK_F8:
							case VK_F9:
							case VK_F10:
							case VK_F11:
							case VK_F12:
							break;
							default:
								ReadConsoleInputW( _consoleIn, &rec, 1, &count );
							continue; // in raw mode, ReadConsoleInput shows shift, ctrl - ignore them
						}
					}
				}

				return ( EVENT_TYPE::KEY_PRESS );
			}
			case ( WAIT_OBJECT_0 + 1 ): {
				ResetEvent( _interrupt );
				if ( _events.empty() ) {
					continue;
				}
				EVENT_TYPE eventType( _events.front() );
				_events.pop_front();
				return ( eventType );
			}
			case ( WAIT_TIMEOUT ): {
				return ( EVENT_TYPE::TIMEOUT );
			}
		}
	}
#else
	pollfd fds[2];
	fds[0].fd = _in_fd;
	fds[0].events = POLLIN;
	fds[1].fd = _interrupt[0];
	fds[1].events = POLLIN;
	int const poll_timeout( timeout_ > 0
		? ( timeout_ > INT_MAX ? INT_MAX : static_cast<int>( timeout_ ) )
		: -1 );
	while ( true ) {
		fds[0].revents = 0;
		fds[1].revents = 0;
		int err( poll( fds, 2, poll_timeout ) );
		if ( ( err == -1 ) && ( errno == EINTR ) ) {
			continue;
		}
		if ( err == 0 ) {
			return ( EVENT_TYPE::TIMEOUT );
		}
		short const ready_mask( POLLIN | POLLHUP | POLLERR | POLLNVAL );
		if ( fds[1].revents & ready_mask ) {
			char data( 0 );
			static_cast<void>( read( _interrupt[0], &data, 1 ) == 1 );
			if ( data == 'k' ) {
				return ( EVENT_TYPE::KEY_PRESS );
			}
			if ( data == 'm' ) {
				return ( EVENT_TYPE::MESSAGE );
			}
			if ( data == 'r' ) {
				return ( EVENT_TYPE::RESIZE );
			}
		}
		if ( fds[0].revents & ready_mask ) {
			return ( EVENT_TYPE::KEY_PRESS );
		}
	}
#endif
}

void Terminal::notify_event( EVENT_TYPE eventType_ ) {
#ifdef _WIN32
	_events.push_back( eventType_ );
	SetEvent( _interrupt );
#else
	char data( ( eventType_ == EVENT_TYPE::KEY_PRESS ) ? 'k' : ( eventType_ == EVENT_TYPE::MESSAGE ? 'm' : 'r' ) );
	static_cast<void>( write( _interrupt[1], &data, 1 ) == 1 );
#endif
}

/**
 * Clear the screen ONLY (no redisplay of anything)
 */
void Terminal::clear_screen( CLEAR_SCREEN clearScreen_ ) {
#ifdef _WIN32
	if ( _autoEscape ) {
#endif
		if ( clearScreen_ == CLEAR_SCREEN::WHOLE ) {
			char const clearCode[] = "\033c\033[H\033[2J\033[0m";
			static_cast<void>( write(_out_fd, clearCode, sizeof ( clearCode ) - 1) >= 0 );
		} else {
			char const clearCode[] = "\033[J";
			static_cast<void>( write(_out_fd, clearCode, sizeof ( clearCode ) - 1) >= 0 );
		}
		return;
#ifdef _WIN32
	}
	COORD coord = { 0, 0 };
	CONSOLE_SCREEN_BUFFER_INFO inf;
	HANDLE consoleOut( _consoleOut != INVALID_HANDLE_VALUE ? _consoleOut : GetStdHandle( STD_OUTPUT_HANDLE ) );
	GetConsoleScreenBufferInfo( consoleOut, &inf );
	if ( clearScreen_ == CLEAR_SCREEN::TO_END ) {
		coord = inf.dwCursorPosition;
		DWORD nWritten( 0 );
		SHORT height( inf.srWindow.Bottom - inf.srWindow.Top );
		DWORD yPos( inf.dwCursorPosition.Y - inf.srWindow.Top );
		DWORD toWrite( ( height + 1 - yPos ) * inf.dwSize.X - inf.dwCursorPosition.X );
//		FillConsoleOutputCharacterA( consoleOut, ' ', toWrite, coord, &nWritten );
		_empty.resize( toWrite - 1, ' ' );
		WriteConsoleA( consoleOut, _empty.data(), toWrite - 1, &nWritten, nullptr );
	} else {
		COORD scrollTarget = { 0, static_cast<SHORT>( -inf.dwSize.Y ) };
		CHAR_INFO fill{ TEXT( ' ' ), inf.wAttributes };
		SMALL_RECT scrollRect = { 0, 0, inf.dwSize.X, inf.dwSize.Y };
		ScrollConsoleScreenBuffer( consoleOut, &scrollRect, nullptr, scrollTarget, &fill );
	}
	SetConsoleCursorPosition( consoleOut, coord );
#endif
}

void Terminal::jump_cursor( int xPos_, int yOffset_ ) {
#ifdef _WIN32
	CONSOLE_SCREEN_BUFFER_INFO inf;
	GetConsoleScreenBufferInfo( _consoleOut, &inf );
	inf.dwCursorPosition.X = xPos_;
	inf.dwCursorPosition.Y += yOffset_;
	SetConsoleCursorPosition( _consoleOut, inf.dwCursorPosition );
#else
	char seq[64];
	if ( yOffset_ != 0 ) { // move the cursor up as required
		snprintf( seq, sizeof seq, "\033[%d%c", abs( yOffset_ ), yOffset_ > 0 ? 'B' : 'A' );
		write8( seq, strlen( seq ) );
	}
	// position at the end of the prompt, clear to end of screen
	snprintf(
		seq, sizeof seq, "\033[%dG",
		xPos_ + 1 /* 1-based on VT100 */
	);
	write8( seq, strlen( seq ) );
#endif
}

#ifdef _WIN32
void Terminal::set_cursor_visible( bool visible_ ) {
	CONSOLE_CURSOR_INFO     cursorInfo;
	GetConsoleCursorInfo( _consoleOut, &cursorInfo );
	cursorInfo.bVisible = visible_;
	SetConsoleCursorInfo( _consoleOut, &cursorInfo );
	return;
}
#else
void Terminal::set_cursor_visible( bool ) {}
#endif

#ifndef _WIN32
int Terminal::read_verbatim( char32_t* buffer_, int size_ ) {
	int len( 0 );
	buffer_[len ++] = read_unicode_character();
	int statusFlags( ::fcntl( _in_fd, F_GETFL, 0 ) );
	::fcntl( _in_fd, F_SETFL, statusFlags | O_NONBLOCK );
	while ( len < size_ ) {
		/* Take only the input that is already available: the descriptor is non-blocking, but `poll` is not. */
		char32_t c( read_unicode_character( -1 ) );
		if ( c == 0 ) {
			break;
		}
		buffer_[len ++] = c;
	}
	::fcntl( _in_fd, F_SETFL, statusFlags );
	return ( len );
}

int Terminal::install_window_change_handler( void ) {
	struct sigaction sa;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = &WindowSizeChanged;

	if (sigaction(SIGWINCH, &sa, &_origWindowChangeHandler) == -1) {
		return errno;
	}
	_windowChangeHandlerInstalled = true;
	if ( _rawMode ) {
		_terminal_.store( this );
	}
	return 0;
}
#endif

}
