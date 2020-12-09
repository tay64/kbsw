// A simple command-line option parser that takes in the syntax
// from a help message string constant.
// For a modern exposition of this approach, go to docopt.org;
// but the author of this code did it before it became mainstream ;-)
// (albeit they didn't call it 'docopt' or actually anything consistent;
// the name comes from the docopt.org initiative).

#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include "docopt.h"

#define DOCOPT_NO_MATCH    0
#define DOCOPT_MATCH       1
#define DOCOPT_ERROR      -1

typedef int Callback( char opt, const char* longopt, unsigned longopt_len, bool has_value,
                      const char* arg, Options* po );

static unsigned LongOptLength( const char* longopt )
{
	size_t len = strcspn(longopt, "= \t\n");
	return ((longopt[len] != '\n') && (longopt[len] != 0)) ? len : 0;
}

static int IterDocOpt( const char* docopt, Callback callback,
                       const char* arg, Options* po )
{
	while( !!(docopt = strstr(docopt, "\n-")) )
	{
		docopt += 2; // skip "\n-"
		const char* longopt = (strncmp(docopt + 1, " --", 3) == 0)
		                    ? docopt + 4
		                    : NULL;
		unsigned longopt_len = longopt ? LongOptLength(longopt) : 0;
		bool has_value = longopt_len ? (longopt[longopt_len] == '=') : false;
		int rc = callback(*docopt, longopt_len ? longopt : NULL, longopt_len, has_value,
		                  arg, po);
		if( rc != DOCOPT_NO_MATCH )  return rc;
	}
	return 0;
}

// for every option that has a default value in the docopt string, pretend
// that this value occurred on command line
static int SetDefaultsCallback( char opt, const char* longopt, unsigned longopt_len,
                                bool has_value, const char* arg, Options* po )
{
	// return nonzero on error (which would mean inconsistent docopt/AppDocOptSetOption)
	return has_value && !AppDocOptSetOption(po, opt, longopt + longopt_len + 1);
}

static int ParseArgumentCallback( char opt, const char* longopt, unsigned longopt_len,
                                  bool has_value, const char* arg, Options* po )
{
	bool match;
	const char* arg_val;

	if( arg[0] != '-' )  return -1; // just a precaution: should not happen

	if( arg[1] == '-' )
	{
		// "--opt" or "--opt=VALUE"
		const char* key = arg + 2;
		const char* eq = strchr(key, '=');
		unsigned key_len = eq ? eq - key : strlen(key);
		match = (key_len == longopt_len) && (strncmp(key, longopt, longopt_len) == 0);
		arg_val = eq ? eq + 1 : NULL;
	}
	else
	{
		// "-x" or "-xVALUE"
		match = (arg[1] == opt);
		arg_val = (match && arg[2]) ? arg + 2 : NULL;
	}

	if( !match )
		return DOCOPT_NO_MATCH;

	if( !!arg_val != has_value )
		return DOCOPT_ERROR;  // value missing when needed, or present when not expected

	if( AppDocOptSetOption(po, opt, arg_val) == false )
		return DOCOPT_ERROR;  // failed to parse value

	return DOCOPT_MATCH;
}

bool DocOptParseCommandLine( Options* po, const char* docopt,
                             int argc, char* argv[] )
{
	bool reported_error = false;
	if( IterDocOpt(docopt, SetDefaultsCallback, NULL, po) != 0 )  return false;
	for( int i = 1; i < argc; ++i )
	{
		bool ok = (argv[i][0] == '-')
		        ? (IterDocOpt(docopt, ParseArgumentCallback, argv[i], po) == DOCOPT_MATCH)
		        : AppDocOptSetOption(po, 0, argv[i]);

		if( !ok && !reported_error )
		{
			AppDocOptReportError(argv[i]);
			reported_error = true;
		}
	}
	return !reported_error;
}

const char* DocOptFindLineWithWord( const char* docopt, const char* prefix, const char* keyword )
{
	unsigned kwlen = strlen(keyword);

	while( !!(docopt = strstr(docopt, prefix)) )
	{
		docopt += strlen(prefix);

		const char* nl = strchr(docopt, '\n');
		unsigned line_len = nl ? nl - docopt : strlen(docopt);
		if( line_len < kwlen )  continue;

		for( unsigned i = 0, e = line_len - kwlen; i <= e; ++i )
		{
			if( isspace(*(docopt + i - 1)) &&
			    (memcmp(docopt + i, keyword, kwlen) == 0) &&
				isspace(docopt[i + kwlen]) )
			{
				return docopt;
			}
		}
	}

	return NULL;
}
