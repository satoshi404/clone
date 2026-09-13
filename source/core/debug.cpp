#include "debug.hpp"

#include <stdio.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void TerminalDebug::print( const char *format, ... )
{
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
}

void TerminalDebug::print( PrintColorType color_type, const char *format, ... )
{
	va_list args;
	va_start( args, format );
	printf("\x1b[%dm", color_type);
	vprintf(format, args);
	printf("\x1b[%dm", PrintColorType_Default);
	va_end(args);
}

void TerminalDebug::println( const char *format, ... )
{
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	printf("\n");
	va_end(args);
}

void TerminalDebug::println( PrintColorType color_type, const char *format, ... )
{
	va_list args;
	va_start( args, format );
	printf( "\x1b[%dm", color_type );
	vprintf( format, args );
	printf( "\x1b[%dm\n", PrintColorType_Default );
	va_end(args);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////