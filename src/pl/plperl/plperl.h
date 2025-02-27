/*-------------------------------------------------------------------------
 *
 * plperl.h
 *	  Common include file for PL/Perl files
 *
 * This should be included _AFTER_ postgres.h and system include files
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
 * Portions Copyright (c) 1995, Regents of the University of California
 *
 * src/pl/plperl/plperl.h
 */

#ifndef PL_PERL_H
#define PL_PERL_H

/* stop perl headers from hijacking stdio and other stuff on Windows */
#ifdef WIN32
#define WIN32IO_IS_STDIO
#endif							/* WIN32 */

/*
 * Supply a value of PERL_UNUSED_DECL that will satisfy gcc - the one
 * perl itself supplies doesn't seem to.
 */
#define PERL_UNUSED_DECL pg_attribute_unused()

/*
 * Sometimes perl carefully scribbles on our *printf macros.
 * So we undefine them here and redefine them after it's done its dirty deed.
 */

#ifdef USE_REPL_SNPRINTF
#undef snprintf
#undef vsnprintf
#endif

/*
 * ActivePerl 5.18 and later are MinGW-built, and their headers use GCC's
 * __inline__.  Translate to something MSVC recognizes.
 */
#ifdef _MSC_VER
#define __inline__ inline
/* Work around for using MSVC and Strawberry Perl >= 5.30. */
#define __builtin_expect(expr, val) (expr)
#endif

/*
 * Regarding bool, both PostgreSQL and Perl might use stdbool.h or not,
 * depending on configuration.  If both agree, things are relatively harmless.
 * If not, things get tricky.  If PostgreSQL does but Perl does not, define
 * HAS_BOOL here so that Perl does not redefine bool; this avoids compiler
 * warnings.  If PostgreSQL does not but Perl does, we need to undefine bool
 * after we include the Perl headers; see below.
 */
#ifdef USE_STDBOOL
#define HAS_BOOL 1
#endif

/*
 * Newer versions of the perl headers trigger a lot of warnings with our
 * compiler flags (at least -Wdeclaration-after-statement,
 * -Wshadow=compatible-local are known to be problematic). The system_header
 * pragma hides warnings from within the rest of this file, if supported.
 */
#ifdef HAVE_PRAGMA_GCC_SYSTEM_HEADER
#pragma GCC system_header
#endif

/*
 * Get the basic Perl API.  We use PERL_NO_GET_CONTEXT mode so that our code
 * can compile against MULTIPLICITY Perl builds without including XSUB.h.
 */
#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"

/*
 * We want to include XSUB.h only within .xs files, because on some platforms
 * it undesirably redefines a lot of libc functions.  But it must appear
 * before ppport.h, so use a #define flag to control inclusion here.
 */
#ifdef PG_NEED_PERL_XSUB_H
/*
 * On Windows, port_win32.h defines macros for a lot of these same functions.
 * To avoid compiler warnings when XSUB.h redefines them, #undef our versions.
 */
#ifdef WIN32
#undef accept
#undef bind
#undef connect
#undef fopen
#undef kill
#undef listen
#undef lstat
#undef mkdir
#undef open
#undef putenv
#undef recv
#undef rename
#undef select
#undef send
#undef socket
#undef stat
#undef unlink
#undef vfprintf
#endif

#include "XSUB.h"
#endif

/* put back our snprintf and vsnprintf */
#ifdef USE_REPL_SNPRINTF
#ifdef snprintf
#undef snprintf
#endif
#ifdef vsnprintf
#undef vsnprintf
#endif
#ifdef __GNUC__
#define vsnprintf(...)	pg_vsnprintf(__VA_ARGS__)
#define snprintf(...)	pg_snprintf(__VA_ARGS__)
#else
#define vsnprintf		pg_vsnprintf
#define snprintf		pg_snprintf
#endif							/* __GNUC__ */
#endif							/* USE_REPL_SNPRINTF */

/* perl version and platform portability */
#include "ppport.h"

/*
 * perl might have included stdbool.h.  If we also did that earlier (see c.h),
 * then that's fine.  If not, we probably rejected it for some reason.  In
 * that case, undef bool and proceed with our own bool.  (Note that stdbool.h
 * makes bool a macro, but our own replacement is a typedef, so the undef
 * makes ours visible again).
 */
#ifndef USE_STDBOOL
#ifdef bool
#undef bool
#endif
#endif

/* supply HeUTF8 if it's missing - ppport.h doesn't supply it, unfortunately */
#ifndef HeUTF8
#define HeUTF8(he)			   ((HeKLEN(he) == HEf_SVKEY) ?			   \
								SvUTF8(HeKEY_sv(he)) :				   \
								(U32)HeKUTF8(he))
#endif

/* supply GvCV_set if it's missing - ppport.h doesn't supply it, unfortunately */
#ifndef GvCV_set
#define GvCV_set(gv, cv)		(GvCV(gv) = cv)
#endif

/* Perl 5.19.4 changed array indices from I32 to SSize_t */
#if PERL_BCDVERSION >= 0x5019004
#define AV_SIZE_MAX SSize_t_MAX
#else
#define AV_SIZE_MAX I32_MAX
#endif

/* declare routines from plperl.c for access by .xs files */
HV		   *plperl_spi_exec(char *, int);
void		plperl_return_next(SV *);
SV		   *plperl_spi_query(char *);
SV		   *plperl_spi_fetchrow(char *);
SV		   *plperl_spi_prepare(char *, int, SV **);
HV		   *plperl_spi_exec_prepared(char *, HV *, int, SV **);
SV		   *plperl_spi_query_prepared(char *, int, SV **);
void		plperl_spi_freeplan(char *);
void		plperl_spi_cursor_close(char *);
void		plperl_spi_commit(void);
void		plperl_spi_rollback(void);
char	   *plperl_sv_to_literal(SV *, char *);
void		plperl_util_elog(int level, SV *msg);

#endif							/* PL_PERL_H */
