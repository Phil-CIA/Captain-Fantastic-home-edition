import subprocess

Import("env")


def run_git(repo_root, args):
    try:
        output = subprocess.check_output(
            ["git", "-C", repo_root] + args,
            stderr=subprocess.DEVNULL,
            text=True,
        )
        return output.strip()
    except Exception:
        return ""


def c_string_literal(value):
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return '\\"%s\\"' % escaped


project_dir = env.subst("$PROJECT_DIR")
repo_root = env.subst("$PROJECT_DIR/..")

# Fall back to project dir if parent is not a git repo.
git_head = run_git(repo_root, ["rev-parse", "--short", "HEAD"])
if not git_head:
    repo_root = project_dir
    git_head = run_git(repo_root, ["rev-parse", "--short", "HEAD"])

if not git_head:
    git_head = "unknown"

status = run_git(repo_root, ["status", "--porcelain"])
state = "dirty" if status else "clean"

env.Append(
    CPPDEFINES=[
        ("CAPTAIN_BUILD_GIT_HASH", c_string_literal(git_head)),
        ("CAPTAIN_BUILD_GIT_STATE", c_string_literal(state)),
    ]
)
