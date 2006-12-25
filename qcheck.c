/*
 * Copyright 2005-2006 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qcheck.c,v 1.31 2006/12/25 22:30:22 vapier Exp $
 *
 * Copyright 2005-2006 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2006 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifdef APPLET_qcheck

#define QCHECK_FLAGS "au" COMMON_FLAGS
static struct option const qcheck_long_opts[] = {
	{"all",     no_argument, NULL, 'a'},
	{"update",  no_argument, NULL, 'u'},
	COMMON_LONG_OPTS
};
static const char *qcheck_opts_help[] = {
	"List all packages",
	"Update chksum and mtimes for packages",
	COMMON_OPTS_HELP
};
static const char qcheck_rcsid[] = "$Id: qcheck.c,v 1.31 2006/12/25 22:30:22 vapier Exp $";
#define qcheck_usage(ret) usage(ret, QCHECK_FLAGS, qcheck_long_opts, qcheck_opts_help, lookup_applet_idx("qcheck"))


int qcheck_main(int argc, char **argv)
{
	DIR *dir, *dirp;
	int i;
	struct dirent *dentry, *de;
	char search_all = 0;
	char qc_update = 0;
	struct stat st;
	size_t num_files, num_files_ok, num_files_unknown;
	char buf[_Q_PATH_MAX], filename[_Q_PATH_MAX];
	char buffer[_Q_PATH_MAX];

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QCHECK, qcheck, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qcheck)
		case 'a': search_all = 1; break;
		case 'u': qc_update = 1; break;
		}
	}
	if ((argc == optind) && !search_all)
		qcheck_usage(EXIT_FAILURE);

	if (chdir(portroot))
		errp("could not chdir(%s) for ROOT", portroot);

	if (chdir(portvdb) != 0 || (dir = opendir(".")) == NULL)
		return EXIT_FAILURE;

	/* open /var/db/pkg */
	while ((dentry = q_vdb_get_next_dir(dir))) {
		if (chdir(dentry->d_name) != 0)
			continue;
		if ((dirp = opendir(".")) == NULL)
			continue;

		/* open the cateogry */
		while ((de = readdir(dirp)) != NULL) {
			FILE *fp, *fpx;
			if (*de->d_name == '.')
				continue;

			fp = fpx = NULL;

			/* see if this cat/pkg is requested */
			if (!search_all) {
				for (i = optind; i < argc; ++i) {
					snprintf(buf, sizeof(buf), "%s/%s", dentry->d_name,
					         de->d_name);
					if (rematch(argv[i], buf, REG_EXTENDED) == 0)
						break;
					if (rematch(argv[i], de->d_name, REG_EXTENDED) == 0)
						break;
				}
				if (i == argc)
					continue;
			}

			snprintf(buf, sizeof(buf), "%s%s/%s/%s/CONTENTS", portroot, portvdb,
			         dentry->d_name, de->d_name);
			if ((fp = fopen(buf, "r")) == NULL)
				continue;
			strncat(buf, "~", sizeof(buf));
			num_files = num_files_ok = num_files_unknown = 0;
			printf("%sing %s%s/%s%s ...\n",
				(qc_update ? "Updat" : "Check"),
				GREEN, dentry->d_name, de->d_name, NORM);
			if (qc_update) {
				if ((fpx = fopen(buf, "w")) == NULL) {
					fclose(fp);
					warn("unable to fopen(%s, w): %s", buf, strerror(errno));
					continue;
				}
			}
			while ((fgets(buf, sizeof(buf), fp)) != NULL) {
				contents_entry *e;

				strcpy(buffer, buf);
				e = contents_parse_line(buf);
				if (!e)
					continue;

				if (strcmp(portroot, "/") != 0) {
					snprintf(filename, sizeof(filename), "%s%s", portroot, e->name);
					e->name = filename;
				}

				/* run our little checks */
				++num_files;
				if (lstat(e->name, &st)) {
					/* make sure file exists */
					printf(" %sAFK%s: %s\n", RED, NORM, e->name);
					continue;
				}
				if (e->digest && S_ISREG(st.st_mode)) {
					/* validate digest (handles MD5 / SHA1) */
					uint8_t hash_algo;
					char *hashed_file;
					switch (strlen(e->digest)) {
						case 32: hash_algo = HASH_MD5; break;
						case 40: hash_algo = HASH_SHA1; break;
						default: hash_algo = 0; break;
					}
					if (!hash_algo) {
						printf(" %sUNKNOWN DIGEST%s: '%s' for '%s'\n", RED, NORM, e->digest, e->name);
						++num_files_unknown;
						continue;
					}
					hashed_file = (char*)hash_file(e->name, hash_algo);
					if (!hashed_file) {
						if (qc_update) {
							fputs(buffer, fpx);
							if (!verbose)
								continue;
						}
						printf(" %sPERM %4o%s: %s\n", RED, (st.st_mode & 07777), NORM, e->name);
						++num_files_unknown;
						continue;
					} else if (strcmp(e->digest, hashed_file)) {
						const char *digest_disp;
						if (qc_update) {
							fprintf(fpx, "obj %s %s %lu\n", e->name, hashed_file, st.st_mtime);
							if (!verbose)
								goto free_and_more_hash;
						}
						switch (hash_algo) {
							case HASH_MD5:  digest_disp = "MD5"; break;
							case HASH_SHA1: digest_disp = "SHA1"; break;
							default:        digest_disp = "UNK"; break;
						}
						printf(" %s%s-DIGEST%s: %s", RED, digest_disp, NORM, e->name);
						if (verbose)
							printf(" (recorded '%s' != actual '%s')", e->digest, hashed_file);
						printf("\n");
free_and_more_hash:
						free(hashed_file);
						continue;
					} else if (e->mtime && e->mtime != st.st_mtime) {
						printf(" %sMTIME%s: %s", RED, NORM, e->name);
						if (verbose)
							printf(" (recorded '%lu' != actual '%lu')", e->mtime, (unsigned long)st.st_mtime);
						printf("\n");

						/* This can only be an obj, dir and sym have no digest */
						if (qc_update)
							fprintf(fpx, "obj %s %s %lu\n", e->name, e->digest, st.st_mtime);

						continue;
					} else {
						if (qc_update)
							fputs(buffer, fpx);
						free(hashed_file);
					}
				} else if (e->mtime && e->mtime != st.st_mtime) {
					printf(" %sMTIME%s: %s", RED, NORM, e->name);
					if (verbose)
						printf(" (recorded '%lu' != actual '%lu')", e->mtime, (unsigned long)st.st_mtime);
					printf("\n");

					/* This can only be a sym */
					if (qc_update)
						fprintf(fpx, "sym %s -> %s %lu\n", e->name, e->sym_target, st.st_mtime);

					continue;
				} else {
					if (qc_update)
						fputs(buffer, fpx);
				}
				++num_files_ok;
			}
			fclose(fp);
			if (qc_update) {
				fclose(fpx);
				snprintf(buf, sizeof(buf), "%s%s/%s/%s/CONTENTS", portroot, portvdb,
					dentry->d_name, de->d_name);
				strcpy(buffer, buf);
				strncat(buffer, "~", sizeof(buffer));
				rename(buffer, buf);
				if (!verbose)
					continue;
			}
			printf("  %2$s*%1$s %3$s%4$lu%1$s out of %3$s%5$lu%1$s file%6$s are good\n",
			       NORM, BOLD, BLUE,
			       (unsigned long)num_files_ok,
			       (unsigned long)num_files,
			       (num_files > 1 ? "s" : ""));
			if (num_files_unknown)
				printf("  %2$s*%1$s Unable to digest %3$s%4$lu%1$s file%5$s\n",
				       NORM, BOLD, BLUE, (unsigned long)num_files_unknown,
				       (num_files_unknown > 1 ? "s" : ""));
		}
		closedir(dirp);
		chdir("..");
	}

	return EXIT_SUCCESS;
}

#else
DEFINE_APPLET_STUB(qcheck)
#endif
