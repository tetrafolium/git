#include "builtin.h"
#include "parse-options.h"
#include "tag.h"
#include "replace-object.h"
#include "object-store.h"
#include "fsck.h"
#include "config.h"

static char const *const builtin_mktag_usage[] = { N_("git mktag"), NULL };
static int option_strict = 1;

static struct fsck_options fsck_options = FSCK_OPTIONS_STRICT;

static int mktag_config(const char *var, const char *value, void *cb)
{
	return fsck_config_internal(var, value, cb, &fsck_options);
}

static int mktag_fsck_error_func(struct fsck_options *o,
				 const struct object_id *oid,
				 enum object_type object_type, int msg_type,
				 const char *message)
{
	switch (msg_type) {
	case FSCK_WARN:
		if (!option_strict) {
			fprintf_ln(
				stderr,
				_("warning: tag input does not pass fsck: %s"),
				message);
			return 0;
		}
		/* fallthrough */
	case FSCK_ERROR:
		/*
		 * We treat both warnings and errors as errors, things
		 * like missing "tagger" lines are "only" warnings
		 * under fsck, we've always considered them an error.
		 */
		fprintf_ln(stderr, _("error: tag input does not pass fsck: %s"),
			   message);
		return 1;
	default:
		BUG(_("%d (FSCK_IGNORE?) should never trigger this callback"),
		    msg_type);
	}
}

static int verify_object_in_tag(struct object_id *tagged_oid, int *tagged_type)
{
	int ret;
	enum object_type type;
	unsigned long size;
	void *buffer;
	const struct object_id *repl;

	buffer = read_object_file(tagged_oid, &type, &size);
	if (!buffer)
		die(_("could not read tagged object '%s'"),
		    oid_to_hex(tagged_oid));
	if (type != *tagged_type)
		die(_("object '%s' tagged as '%s', but is a '%s' type"),
		    oid_to_hex(tagged_oid), type_name(*tagged_type),
		    type_name(type));

	repl = lookup_replace_object(the_repository, tagged_oid);
	ret = check_object_signature(the_repository, repl, buffer, size,
				     type_name(*tagged_type));
	free(buffer);

	return ret;
}

int cmd_mktag(int argc, const char **argv, const char *prefix)
{
	static struct option builtin_mktag_options[] = {
		OPT_BOOL(0, "strict", &option_strict,
			 N_("enable more strict checking")),
		OPT_END(),
	};
	struct strbuf buf = STRBUF_INIT;
	struct object_id tagged_oid;
	int tagged_type;
	struct object_id result;

	argc = parse_options(argc, argv, NULL, builtin_mktag_options,
			     builtin_mktag_usage, 0);

	if (strbuf_read(&buf, 0, 0) < 0)
		die_errno(_("could not read from stdin"));

	fsck_options.error_func = mktag_fsck_error_func;
	fsck_set_msg_type(&fsck_options, "extraheaderentry", "warn");
	/* config might set fsck.extraHeaderEntry=* again */
	git_config(mktag_config, NULL);
	if (fsck_tag_standalone(NULL, buf.buf, buf.len, &fsck_options,
				&tagged_oid, &tagged_type))
		die(_("tag on stdin did not pass our strict fsck check"));

	if (verify_object_in_tag(&tagged_oid, &tagged_type))
		die(_("tag on stdin did not refer to a valid object"));

	if (write_object_file(buf.buf, buf.len, tag_type, &result) < 0)
		die(_("unable to write tag file"));

	strbuf_release(&buf);
	puts(oid_to_hex(&result));
	return 0;
}
