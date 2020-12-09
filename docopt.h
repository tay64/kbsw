#ifndef DOCOPT_H
#define DOCOPT_H

#include <stdbool.h>

// ---- should be defined by the application -----------------------------------

typedef struct Options Options;

// Called to report each option discovered on the command line;
// also to assign default values from the docopt string.
//   `opt`  the option character, or 0 for a non-option argument
//   `val`  - NULL, if option has no value;
//          - option value for an option with value (can have whitespace-delimited
//            junk at the end when called with the default value);
//          - entire non-option argument if `opt` == 0.
// Should return true if succeeded, false in case of error.
bool AppDocOptSetOption( Options* po, char opt, const char* val );

// Called to report a syntax error or a value rejected by DocOptAppSetOption.
void AppDocOptReportError( const char* bad_arg );

// ---- provided by docopt.c ---------------------------------------------------

// Parses the `argc`/`argv[]` pair (skipping `argv[0]`),
// using `docopt` as the syntax definition
// and calling `DocOptSetOption` to fill in the structure pointed to by `po`.
// On error, calls `DocOptAppReportError` and returns false.
// On success, return true.
bool DocOptParseCommandLine( Options* po, const char* docopt,
                             int argc, char* argv[] );


// A helper for organizing keyword handling.
// Searches `docopt`, line by line, for the occurrence of `keyword` in a space-
// delimited context.
// Considers only lines starting with `prefix`, which should begin with a `\n`.
// If found, returns a pointer to the beginning of the line containing `keyword`
// after skipping `prefix`.
// If not found, returns NULL.
const char* DocOptFindLineWithWord( const char* docopt, const char* prefix,
                                    const char* keyword );

#endif
