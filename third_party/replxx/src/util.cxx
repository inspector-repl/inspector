#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <wctype.h>

#include "util.hxx"
#include "terminal.hxx"

#undef min

namespace replxx {

int mk_wcwidth( char32_t );

int virtual_render( char32_t const* display_, int size_, int& x_, int& y_, int screenColumns_, int promptLen_, UnicodeString* rendered_, int* lastLineStart_, int out_fd_) {
	int visibleCount( 0 );
	auto render = [&rendered_, &visibleCount]( char32_t c_, bool visible_, bool renderAttributes_ = true ) {
		if ( rendered_ && renderAttributes_ ) {
			rendered_->push_back( c_ );
			if ( visible_ ) {
				++ visibleCount;
			}
		}
	};
	auto start_line = [&rendered_, &lastLineStart_]() {
		if ( rendered_ && lastLineStart_ ) {
			*lastLineStart_ = rendered_->length();
		}
	};
	bool wrapped( false );
	auto advance_cursor = [&x_, &y_, &screenColumns_, &wrapped, &start_line]( int by_ = 1 ) {
		wrapped = false;
		x_ += by_;
		if ( x_ >= screenColumns_ ) {
			x_ = 0;
			++ y_;
			wrapped = true;
			start_line();
		}
	};
	bool const renderAttributes( !!tty::is_a_tty(out_fd_) );
	int pos( 0 );
	while ( pos < size_ ) {
		char32_t c( display_[pos] );
		if ( ( c == '\n' ) || ( c == '\r' ) ) {
			render( c, true );
			if ( ( c == '\n' ) && ! wrapped ) {
				++ y_;
			}
			x_ = promptLen_;
			start_line();
			++ pos;
			continue;
		}
		if ( c == '\b' ) {
			render( c, true );
			-- x_;
			if ( x_ < 0 ) {
				x_ = screenColumns_ - 1;
				-- y_;
			}
			++ pos;
			continue;
		}
		if ( c == '\033' ) {
			render( c, false, renderAttributes );
			++ pos;
			if ( pos >= size_ ) {
				advance_cursor( 2 );
				continue;
			}
			c = display_[pos];
			if ( c != '[' ) {
				advance_cursor( 2 );
				continue;
			}
			render( c, false, renderAttributes );
			++ pos;
			if ( pos >= size_ ) {
				advance_cursor( 3 );
				continue;
			}
			int codeLen( 0 );
			while ( pos < size_ ) {
				c = display_[pos];
				if ( ( c != ';' ) && ( ( c < '0' ) || ( c > '9' ) ) ) {
					break;
				}
				render( c, false, renderAttributes );
				++ codeLen;
				++ pos;
			}
			if ( pos >= size_ ) {
				continue;
			}
			c = display_[pos];
			if ( c != 'm' ) {
				advance_cursor( 3 + codeLen );
				continue;
			}
			render( c, false, renderAttributes );
			++ pos;
			continue;
		}
		if ( c == '\t' ) {
			render( ' ', true );
			render( ' ', true );
			render( ' ', true );
			render( ' ', true );
			advance_cursor( 4 );
			++ pos;
			continue;
		}
		if ( is_control_code( c ) ) {
			render( c, true );
			advance_cursor( 2 );
			++ pos;
			continue;
		}
		int wcw( mk_wcwidth( c ) );
		if ( wcw < 0 ) {
			break;
		}
		render( c, true );
		advance_cursor( wcw );
		++ pos;
	}
	return ( visibleCount );
}

char const* ansi_color( Replxx::Color color_ ) {
	int unsigned code( static_cast<int unsigned>( color_ ) );
	int unsigned fg( code & 0xFFu );
	int unsigned bg( ( code >> 8 ) & 0xFFu );
	char const* bold( ( code & color::BOLD ) != 0 ? ";1" : "" );
	char const* underline = ( ( code & color::UNDERLINE ) != 0 ? ";4" : "" );
	static int const MAX_COLOR_CODE_SIZE( 32 );
	/* Per thread: the function is called concurrently, and the result is consumed right away. */
	thread_local char colorBuffer[MAX_COLOR_CODE_SIZE];
	int pos( 0 );
	if ( ( code & static_cast<int unsigned>( Replxx::Color::DEFAULT ) ) != 0 ) {
		pos = snprintf( colorBuffer, MAX_COLOR_CODE_SIZE, "\033[0%s%sm", underline, bold );
	} else if ( fg <= static_cast<int unsigned>( Replxx::Color::LIGHTGRAY ) ) {
		pos = snprintf( colorBuffer, MAX_COLOR_CODE_SIZE, "\033[0;22;3%d%s%sm", fg, underline, bold );
	} else if ( fg <= static_cast<int unsigned>( Replxx::Color::WHITE ) ) {
		/* Aixterm bright color codes (90-97) belong to the basic 16-color set and are supported
		 * by virtually every terminal in use. Previously they were emitted only when TERM
		 * contained "256" (they are unrelated to 256-color support), with a fallback to
		 * bold + dark color otherwise. That broke bright colors on terminals with TERM values
		 * like xterm-ghostty, xterm-kitty, alacritty or foot: modern terminals do not render
		 * bold as bright by default, so e.g. GRAY (bold black) degraded to plain black,
		 * invisible on a dark background. */
		pos = snprintf( colorBuffer, MAX_COLOR_CODE_SIZE, "\033[0;9%d%s%sm", fg - static_cast<int>( Replxx::Color::GRAY ), underline, bold );
	} else {
		pos = snprintf( colorBuffer, MAX_COLOR_CODE_SIZE, "\033[0;38;5;%d%s%sm", fg, underline, bold );
	}
	if ( ( code & color::BACKGROUND_COLOR_SET ) == 0 ) {
		return colorBuffer;
	}
	if ( bg <= static_cast<int unsigned>( Replxx::Color::WHITE ) ) {
		if ( bg <= static_cast<int unsigned>( Replxx::Color::LIGHTGRAY ) ) {
			snprintf( colorBuffer + pos, MAX_COLOR_CODE_SIZE - pos, "\033[4%dm", bg );
		} else {
			snprintf( colorBuffer + pos, MAX_COLOR_CODE_SIZE - pos, "\033[10%dm", bg - static_cast<int>( Replxx::Color::GRAY ) );
		}
	} else {
		snprintf( colorBuffer + pos, MAX_COLOR_CODE_SIZE - pos, "\033[48;5;%dm", bg );
	}
	return colorBuffer;
}

std::string now_ms_str( void ) {
	std::chrono::milliseconds ms( std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() ) );
	time_t t( ms.count() / 1000 );
	tm broken;
#ifdef _WIN32
#define localtime_r( t, b ) localtime_s( ( b ), ( t ) )
#endif
	localtime_r( &t, &broken );
#undef localtime_r
	static int const BUFF_SIZE( 32 );
	char str[BUFF_SIZE];
	strftime( str, BUFF_SIZE, "%Y-%m-%d %H:%M:%S.", &broken );
	snprintf( str + sizeof ( "YYYY-mm-dd HH:MM:SS" ), 5, "%03d", static_cast<int>( ms.count() % 1000 ) );
	return ( str );
}

}

