#include <linux/types.h>
#include <linux/user_namespace.h>

#include "supercall/internal.h"
#include "manager/manager_identity.h"
#include "policy/allowlist.h"

// Permission check functions
bool only_manager(void)
{
	return current_user_ns() == &init_user_ns && is_manager();
}

bool only_root(void)
{
	return current_user_ns() == &init_user_ns && current_uid().val == 0;
}

bool manager_or_root(void)
{
	return current_user_ns() == &init_user_ns && (current_uid().val == 0 || is_manager());
}

bool always_allow(void)
{
	return true; // No permission check
}

bool allowed_for_su(void)
{
	if (current_user_ns() != &init_user_ns)
		return false;
	bool is_allowed = is_manager() || ksu_is_allow_uid_for_current(current_uid().val);
	return is_allowed;
}
