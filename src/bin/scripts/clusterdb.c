/*-------------------------------------------------------------------------
 *
 * clusterdb
 *
 * Portions Copyright (c) 2002-2018, PostgreSQL Global Development Group
 *
 * src/bin/scripts/clusterdb.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres_fe.h"
#include "common.h"
#include "fe_utils/simple_list.h"
#include "fe_utils/string_utils.h"


static void cluster_one_database(const ConnParams *cparams, const char *table,
								 const char *progname, bool verbose, bool echo);
static void cluster_all_databases(ConnParams *cparams, const char *progname,
								  bool verbose, bool echo, bool quiet);
static void help(const char *progname);


int
main(int argc, char *argv[])
{
	static struct option long_options[] = {
		{"host", required_argument, NULL, 'h'},
		{"port", required_argument, NULL, 'p'},
		{"username", required_argument, NULL, 'U'},
		{"no-password", no_argument, NULL, 'w'},
		{"password", no_argument, NULL, 'W'},
		{"echo", no_argument, NULL, 'e'},
		{"quiet", no_argument, NULL, 'q'},
		{"dbname", required_argument, NULL, 'd'},
		{"all", no_argument, NULL, 'a'},
		{"table", required_argument, NULL, 't'},
		{"verbose", no_argument, NULL, 'v'},
		{"maintenance-db", required_argument, NULL, 2},
		{NULL, 0, NULL, 0}
	};

	const char *progname;
	int			optindex;
	int			c;

	const char *dbname = NULL;
	const char *maintenance_db = NULL;
	char	   *host = NULL;
	char	   *port = NULL;
	char	   *username = NULL;
	enum trivalue prompt_password = TRI_DEFAULT;
	ConnParams	cparams;
	bool		echo = false;
	bool		quiet = false;
	bool		alldb = false;
	bool		verbose = false;
	SimpleStringList tables = {NULL, NULL};

	progname = get_progname(argv[0]);
	set_pglocale_pgservice(argv[0], PG_TEXTDOMAIN("pgscripts"));

	handle_help_version_opts(argc, argv, "clusterdb", help);

	while ((c = getopt_long(argc, argv, "h:p:U:wWeqd:at:v", long_options, &optindex)) != -1)
	{
		switch (c)
		{
			case 'h':
				host = pg_strdup(optarg);
				break;
			case 'p':
				port = pg_strdup(optarg);
				break;
			case 'U':
				username = pg_strdup(optarg);
				break;
			case 'w':
				prompt_password = TRI_NO;
				break;
			case 'W':
				prompt_password = TRI_YES;
				break;
			case 'e':
				echo = true;
				break;
			case 'q':
				quiet = true;
				break;
			case 'd':
				dbname = pg_strdup(optarg);
				break;
			case 'a':
				alldb = true;
				break;
			case 't':
				simple_string_list_append(&tables, optarg);
				break;
			case 'v':
				verbose = true;
				break;
			case 2:
				maintenance_db = pg_strdup(optarg);
				break;
			default:
				fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
				exit(1);
		}
	}

	/*
	 * Non-option argument specifies database name as long as it wasn't
	 * already specified with -d / --dbname
	 */
	if (optind < argc && dbname == NULL)
	{
		dbname = argv[optind];
		optind++;
	}

	if (optind < argc)
	{
		fprintf(stderr, _("%s: too many command-line arguments (first is \"%s\")\n"),
				progname, argv[optind]);
		fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
		exit(1);
	}

	/* fill cparams except for dbname, which is set below */
	cparams.pghost = host;
	cparams.pgport = port;
	cparams.pguser = username;
	cparams.prompt_password = prompt_password;
	cparams.override_dbname = NULL;

	setup_cancel_handler();

	if (alldb)
	{
		if (dbname)
		{
			fprintf(stderr, _("%s: cannot cluster all databases and a specific one at the same time\n"),
					progname);
			exit(1);
		}

		if (tables.head != NULL)
		{
			fprintf(stderr, _("%s: cannot cluster specific table(s) in all databases\n"),
					progname);
			exit(1);
		}

		cparams.dbname = maintenance_db;

		cluster_all_databases(&cparams, progname, verbose, echo, quiet);
	}
	else
	{
		if (dbname == NULL)
		{
			if (getenv("PGDATABASE"))
				dbname = getenv("PGDATABASE");
			else if (getenv("PGUSER"))
				dbname = getenv("PGUSER");
			else
				dbname = get_user_name_or_exit(progname);
		}

		cparams.dbname = dbname;

		if (tables.head != NULL)
		{
			SimpleStringListCell *cell;

			for (cell = tables.head; cell; cell = cell->next)
			{
				cluster_one_database(&cparams, cell->val,
									 progname, verbose, echo);
			}
		}
		else
			cluster_one_database(&cparams, NULL,
								 progname, verbose, echo);
	}

	exit(0);
}


static void
cluster_one_database(const ConnParams *cparams, const char *table,
					 const char *progname, bool verbose, bool echo)
{
	PQExpBufferData sql;

	PGconn	   *conn;

	conn = connectDatabase(cparams, progname, echo, false, false);

	initPQExpBuffer(&sql);

	appendPQExpBufferStr(&sql, "CLUSTER");
	if (verbose)
		appendPQExpBufferStr(&sql, " VERBOSE");
	if (table)
	{
		appendPQExpBufferChar(&sql, ' ');
		appendQualifiedRelation(&sql, table, conn, progname, echo);
	}
	appendPQExpBufferChar(&sql, ';');

	if (!executeMaintenanceCommand(conn, sql.data, echo))
	{
		if (table)
			fprintf(stderr, _("%s: clustering of table \"%s\" in database \"%s\" failed: %s"),
					progname, table, PQdb(conn), PQerrorMessage(conn));
		else
			fprintf(stderr, _("%s: clustering of database \"%s\" failed: %s"),
					progname, PQdb(conn), PQerrorMessage(conn));
		PQfinish(conn);
		exit(1);
	}
	PQfinish(conn);
	termPQExpBuffer(&sql);
}


static void
cluster_all_databases(ConnParams *cparams, const char *progname,
					  bool verbose, bool echo, bool quiet)
{
	PGconn	   *conn;
	PGresult   *result;
	int			i;

	conn = connectMaintenanceDatabase(cparams, progname, echo);
	result = executeQuery(conn,
						  "SELECT datname FROM pg_database WHERE datallowconn AND datconnlimit <> -2 ORDER BY 1;",
						  progname, echo);
	PQfinish(conn);

	for (i = 0; i < PQntuples(result); i++)
	{
		char	   *dbname = PQgetvalue(result, i, 0);

		if (!quiet)
		{
			printf(_("%s: clustering database \"%s\"\n"), progname, dbname);
			fflush(stdout);
		}

		cparams->override_dbname = dbname;

		cluster_one_database(cparams, NULL, progname, verbose, echo);
	}

	PQclear(result);
}


static void
help(const char *progname)
{
	printf(_("%s clusters all previously clustered tables in a database.\n\n"), progname);
	printf(_("Usage:\n"));
	printf(_("  %s [OPTION]... [DBNAME]\n"), progname);
	printf(_("\nOptions:\n"));
	printf(_("  -a, --all                 cluster all databases\n"));
	printf(_("  -d, --dbname=DBNAME       database to cluster\n"));
	printf(_("  -e, --echo                show the commands being sent to the server\n"));
	printf(_("  -q, --quiet               don't write any messages\n"));
	printf(_("  -t, --table=TABLE         cluster specific table(s) only\n"));
	printf(_("  -v, --verbose             write a lot of output\n"));
	printf(_("  -V, --version             output version information, then exit\n"));
	printf(_("  -?, --help                show this help, then exit\n"));
	printf(_("\nConnection options:\n"));
	printf(_("  -h, --host=HOSTNAME       database server host or socket directory\n"));
	printf(_("  -p, --port=PORT           database server port\n"));
	printf(_("  -U, --username=USERNAME   user name to connect as\n"));
	printf(_("  -w, --no-password         never prompt for password\n"));
	printf(_("  -W, --password            force password prompt\n"));
	printf(_("  --maintenance-db=DBNAME   alternate maintenance database\n"));
	printf(_("\nRead the description of the SQL command CLUSTER for details.\n"));
	printf(_("\nReport bugs to <pgsql-bugs@postgresql.org>.\n"));
}
