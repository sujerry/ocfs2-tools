/*
 * mount.ocfs2.c  Mounts ocfs2 volume
 *
 * Copyright (C) 2005 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 */

#include "mount.ocfs2.h"
#include "o2cb.h"

int verbose = 0;
int mount_quiet = 0;
char *progname = NULL;

static int nomtab = 0;

struct mount_options {
	char *dev;
	char *dir;
	char *opts;
	int flags;
	char *xtra_opts;
};

static void read_options(int argc, char **argv, struct mount_options *mo)
{
	int c;

	progname = basename(argv[0]);

	if (argc < 2)
		goto bail;

	while(1) {
		c = getopt(argc, argv, "vno:");
		if (c == -1)
			break;

		switch (c) {
		case 'v':
			++verbose;
			break;

		case 'n':
			++nomtab;
			break;

		case 'o':
			if (optarg)
				mo->opts = xstrdup(optarg);
			break;

		default:
			break;
		}
	}

	if (optind < argc && argv[optind])
		mo->dev = xstrdup(argv[optind]);

	++optind;

	if (optind < argc && argv[optind])
		mo->dir = xstrdup(argv[optind]);

bail:
	return ;
}

/*
 * Code based on similar function in util-linux-2.12p/mount/mount.c
 *
 */
static void print_one(const struct my_mntent *me)
{
	if (mount_quiet)
		return ;

	printf ("%s on %s", me->mnt_fsname, me->mnt_dir);

	if (me->mnt_type != NULL && *(me->mnt_type) != '\0')
		printf (" type %s", me->mnt_type);

	if (me->mnt_opts)
		printf (" (%s)", me->mnt_opts);

	printf ("\n");
}


static void my_free(const void *s)
{
	if (s)
		free((void *) s);
}

/*
 * Code based on similar function in util-linux-2.12p/mount/mount.c
 *
 */
static void update_mtab_entry(char *spec, char *node, char *type, char *opts,
			      int flags, int freq, int pass)
{
	struct my_mntent mnt;

	mnt.mnt_fsname = canonicalize (spec);
	mnt.mnt_dir = canonicalize (node);
	mnt.mnt_type = type;
	mnt.mnt_opts = opts;
	mnt.mnt_freq = freq;
	mnt.mnt_passno = pass;
      
	/* We get chatty now rather than after the update to mtab since the
	   mount succeeded, even if the write to /etc/mtab should fail.  */
	if (verbose)
		print_one (&mnt);

	if (!nomtab && mtab_is_writable()) {
#if 0
		if (flags & MS_REMOUNT)
			update_mtab (mnt.mnt_dir, &mnt);
		else
#endif
		{
			mntFILE *mfp;

			lock_mtab();

			mfp = my_setmntent(MOUNTED, "a+");
			if (mfp == NULL || mfp->mntent_fp == NULL) {
				com_err(progname, OCFS2_ET_IO, "%s, %s",
					MOUNTED, strerror(errno));
			} else {
				if ((my_addmntent (mfp, &mnt)) == 1) {
					com_err(progname, OCFS2_ET_IO, "%s, %s",
						MOUNTED, strerror(errno));
				}
			}
			my_endmntent(mfp);
			unlock_mtab();
		}
	}
	my_free(mnt.mnt_fsname);
	my_free(mnt.mnt_dir);
}

static int process_options(struct mount_options *mo)
{
	if (!mo->dev) {
		com_err(progname, OCFS2_ET_BAD_DEVICE_NAME, " ");
		return -1;
	}

	if (!mo->dir) {
		com_err(progname, OCFS2_ET_INVALID_ARGUMENT, "no mountpoint specified");
		return -1;
	}

	if (mo->opts)
		parse_opts(mo->opts, &mo->flags, &mo->xtra_opts);

	return 0;
}

static int check_for_hb_ctl(const char *hb_ctl_path)
{
	int ret;

	ret = access(hb_ctl_path, X_OK);
	if (ret < 0) {
		ret = errno;
		return ret;
	}

	return ret;
}

static int run_hb_ctl(const char *hb_ctl_path,
		      const char *device)
{
	int ret = 0;
	int child_status;
	char * argv[5];
	pid_t child;

	child = fork();
	if (child < 0) {
		ret = errno;
		goto bail;
	}

	if (!child) {
		argv[0] = (char *) hb_ctl_path;
		argv[1] = "-S";
		argv[2] = "-d";
		argv[3] = (char *) device;
		argv[4] = NULL;

		ret = execv(argv[0], argv);

		ret = errno;
		exit(ret);
	} else {
		ret = waitpid(child, &child_status, 0);
		if (ret < 0) {
			ret = errno;
			goto bail;
		}

		ret = WEXITSTATUS(child_status);
	}

bail:
	return ret;
}

static int start_heartbeat(const char *hb_ctl_path,
			   const char *device)
{
	int ret;

	ret = check_for_hb_ctl(hb_ctl_path);
	if (ret)
		return ret;

	ret = run_hb_ctl(hb_ctl_path, device);

	return ret;
}

int main(int argc, char **argv)
{
	errcode_t ret = 0;
	struct mount_options mo;
	const char *hb_ctl_path = "/sbin/ocfs2_hb_ctl";

	initialize_ocfs_error_table();
	initialize_o2dl_error_table();
	initialize_o2cb_error_table();

	memset(&mo, 0, sizeof(mo));
	read_options (argc, argv, &mo);

	ret = process_options(&mo);
	if (ret)
		goto bail;

	if (verbose)
		printf("device=%s\n", mo.dev);

	ret = start_heartbeat(hb_ctl_path, mo.dev);
	if (ret) {
		com_err(progname, 0, "Error when attempting to run %s: "
			"\"%s\"\n", hb_ctl_path, strerror(ret));
		goto bail;
	}

	ret = mount(mo.dev, mo.dir, "ocfs2", mo.flags & ~MS_NOSYS, mo.xtra_opts);
	if (ret) {
		fprintf(stderr, "error %d while mounting %s on %s", errno, 
			mo.dev, mo.dir);
		goto bail;
	}

	update_mtab_entry(mo.dev, mo.dir, "ocfs2", mo.opts, mo.flags, 0, 0);

bail:
	free(mo.dev);
	free(mo.dir);
	free(mo.opts);
	free(mo.xtra_opts);

	return ret ? 1 : 0;
}
